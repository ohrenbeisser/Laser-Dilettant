#include "contentcontroller.h"

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>

#include "appsettings.h"
#include "core/imagedocument.h"
#include "core/imageloader.h"
#include "core/textmodel.h"
#include "imagedialog.h"
#include "projectcontroller.h"
#include "textdialog.h"

namespace ld::app {

namespace {
// "*.jpg *.jpeg *.png ..." für den Dateidialog: alle Formate, die auf diesem System ladbar sind.
QString imageFilter()
{
    QStringList patterns;
    for (const QString& suffix : ld::core::supportedImageSuffixes()) patterns << QStringLiteral("*.") + suffix;
    return patterns.join(QLatin1Char(' '));
}
}  // namespace

ContentController::ContentController(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                                     ProjectController* project, std::function<double()> pixelSize,
                                     std::function<bool()> jobActive, std::function<bool()> ncMode, QWidget* dialogParent,
                                     QObject* parent)
    : QObject(parent),
      document_(document),
      machine_(machine),
      project_(project),
      pixelSize_(std::move(pixelSize)),
      jobActive_(std::move(jobActive)),
      ncMode_(std::move(ncMode)),
      dialogParent_(dialogParent)
{
}

bool ContentController::confirmReplaceImage()
{
    if (!document_->hasImage() || !document_->isModified()) {
        return true;  // nichts zu verlieren, keine Rückfrage
    }
    const auto answer = QMessageBox::question(
        dialogParent_, tr("Bild ersetzen"),
        tr("Das Bild wurde verändert (Größe, Drehen oder Spiegeln). "
           "Beim Laden eines anderen Bildes gehen diese Änderungen verloren.\n\nTrotzdem ersetzen?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Yes;
}

void ContentController::setNewImage(const QImage& image, const QString& path, std::shared_ptr<const ld::core::SvgModel> svg)
{
    document_->setImage(image, path, pixelSize_());
    document_->setSvg(std::move(svg));
    document_->setJobOrigin(AppSettings::instance().defaultJobOrigin());   // Standard aus den Einstellungen
    // Ein frisch geladenes Bild gilt als unverändert; bekommt ein vorhandenes Projekt ein anderes Bild, ist es geändert.
    if (project_->hasPath()) {
        document_->markModified();
    } else {
        document_->markSaved();
    }
    // Nach dem Laden folgt der Importdialog (siehe planung.md, Abschnitt 7). "Abbrechen"
    // lässt das Bild mit den Vorgabewerten geladen.
    editImage();
}

void ContentController::editImage()
{
    if (!document_->hasImage()) {
        return;
    }
    ImageDialog dialog(document_, machine_, dialogParent_);
    dialog.exec();
}

void ContentController::openTextDialog()
{
    if (jobActive_ && jobActive_()) {
        QMessageBox::information(dialogParent_, tr("Schrift"), tr("Es läuft gerade ein Auftrag."));
        return;
    }
    const bool editing = document_->textSpec().has_value() && !(ncMode_ && ncMode_());
    TextDialog dialog(machine_, editing ? document_->textSpec() : std::nullopt, dialogParent_);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Ein neuer Text ersetzt das Bild (Rückfrage, wenn das Bild verändert wurde).
    if (!editing && !confirmReplaceImage()) {
        return;
    }
    const ld::core::TextSpec spec = dialog.spec();
    const double pixelSize = editing ? document_->settings().pixelSizeMm : pixelSize_();
    const ld::core::TextContent content = ld::core::makeTextContent(spec, pixelSize);
    if (!content.ok()) {
        QMessageBox::warning(dialogParent_, tr("Schrift"), tr("Aus diesem Text entsteht keine Grafik."));
        return;
    }
    // Beim Bearbeiten bleiben Drehung, Spiegelung und Bildbearbeitung erhalten; die Größe kommt vom Text.
    ld::core::ImageSettings settings = editing ? document_->settings() : ld::core::ImageSettings{};
    settings.pixelSizeMm = pixelSize;
    settings.crop = QRect();
    if (!editing) {
        // Text besteht nur aus Füllungen: Cutting soll deren Umrisse fahren (bei SVG-Dateien ist das aus).
        settings.svg.cutFills = true;
        settings.svg.cutStrokes = false;
    }
    ld::core::ObjectTransform transform = editing ? document_->transform() : ld::core::ObjectTransform{};
    transform.widthMm = content.shape.sizeMm.width();
    transform.heightMm = content.shape.sizeMm.height();
    document_->setImageWithState(content.image, QString(), settings, transform);
    document_->setSvg(content.svg);
    document_->setTextSpec(spec);
    if (!editing) {
        document_->setJobOrigin(AppSettings::instance().defaultJobOrigin());
    }
    document_->markModified();   // der Text steht noch in keiner Datei
    emit contentChanged();
}

void ContentController::insertCalibration(const ld::core::CalibrationSpec& spec, QWidget* messageParent)
{
    if (jobActive_ && jobActive_()) {
        QMessageBox::information(messageParent ? messageParent : dialogParent_, tr("Kalibrierung"),
                                 tr("Es läuft gerade ein Auftrag."));
        return;
    }
    const bool sameCalibration = document_->calibrationSpec().has_value();
    if (!sameCalibration && !confirmReplaceImage()) {
        return;
    }
    const double pixelSize = document_->hasImage() ? document_->settings().pixelSizeMm : pixelSize_();
    const ld::core::CalibrationContent content = ld::core::makeCalibrationContent(spec, pixelSize);
    if (!content.ok()) {
        QMessageBox::warning(messageParent ? messageParent : dialogParent_, tr("Kalibrierung"), tr("Aus diesen Werten entsteht keine Grafik."));
        return;
    }
    ld::core::ImageSettings settings;
    settings.pixelSizeMm = pixelSize;
    settings.svg.cutStrokes = true;
    settings.svg.cutFills = false;
    ld::core::ObjectTransform transform;
    transform.widthMm = content.shape.sizeMm.width();
    transform.heightMm = content.shape.sizeMm.height();
    document_->setImageWithState(content.image, QString(), settings, transform);
    document_->setSvg(content.svg);
    document_->setTextSpec(std::nullopt);
    document_->setCalibrationSpec(spec);
    document_->setJobOrigin(AppSettings::instance().defaultJobOrigin());
    // Linien: Cutting (mit den Werten für Schneiden aus dem rechten Panel).
    document_->setOperation(ld::core::switchMode(document_->operation(), ld::core::EngraveMode::Cutting));
    document_->markModified();
    emit contentChanged();
    emit message(tr("Messgrafik eingefügt. Zum Brennen im Hauptfenster auf Start klicken."), 8000);
}

void ContentController::openImageDialog()
{
    if (!confirmReplaceImage()) {
        return;
    }
    const QString startDir = AppSettings::instance().startFolder(FolderKind::Images);
    const QString path = QFileDialog::getOpenFileName(
        dialogParent_, tr("Bild laden"), startDir,
        tr("Bilder (%1);;Alle Dateien (*)").arg(imageFilter()));
    if (path.isEmpty()) {
        return;
    }
    AppSettings::instance().setLastFolder(FolderKind::Images, path);
    // Die Rückfrage ist oben schon erfolgt; loadImageFromFile fragt nicht noch einmal.
    const ld::core::LoadResult result = ld::core::loadImageFile(path, pixelSize_());
    if (!result.ok()) {
        QMessageBox::warning(dialogParent_, tr("Bild laden"), result.error);
        return;
    }
    setNewImage(result.image, path, result.svg);
}

void ContentController::loadFile(const QString& path)
{
    // Wird von Drag & Drop benutzt (dort gab es noch keine Rückfrage). Eine Projektdatei wird geöffnet.
    if (path.endsWith(QStringLiteral(".ldproj"), Qt::CaseInsensitive)) {
        project_->openProject(path);
        return;
    }
    if (!confirmReplaceImage()) {
        return;
    }
    const ld::core::LoadResult result = ld::core::loadImageFile(path, pixelSize_());
    if (!result.ok()) {
        QMessageBox::warning(dialogParent_, tr("Bild laden"), result.error);
        return;
    }
    setNewImage(result.image, path, result.svg);
}

void ContentController::pasteImage()
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    // Zuerst ein echtes Bild in der Zwischenablage, sonst eine kopierte Bilddatei (Dateimanager).
    QImage image = QGuiApplication::clipboard()->image();
    QString path;
    if (image.isNull() && mime && mime->hasUrls() && mime->urls().first().isLocalFile()) {
        path = mime->urls().first().toLocalFile();
        const ld::core::LoadResult result = ld::core::loadImageFile(path, pixelSize_());
        if (result.ok()) {
            image = result.image;
        }
    }
    if (image.isNull()) {
        emit message(tr("Die Zwischenablage enthält kein Bild."), 4000);
        return;
    }
    if (!confirmReplaceImage()) {
        return;
    }
    setNewImage(ld::core::flattenOnWhite(image), path);
}

}  // namespace ld::app
