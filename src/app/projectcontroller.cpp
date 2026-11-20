#include "projectcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include "appsettings.h"
#include "core/imagedocument.h"
#include "core/imageloader.h"
#include "core/project.h"
#include "core/textmodel.h"

namespace ld::app {

using ld::core::ProjectData;

namespace {
const QString kRecoveryFile = QStringLiteral("autosave.ldproj");
const QString kRecoveryImage = QStringLiteral("autosave-bild.png");
const QString kSessionFile = QStringLiteral("session.ldproj");
const QString kSessionImage = QStringLiteral("session-bild.png");
}  // namespace

ProjectController::ProjectController(ld::core::ImageDocument* document, QWidget* dialogParent,
                                     std::function<QString()> materialName, QObject* parent)
    : QObject(parent), document_(document), parentWidget_(dialogParent), materialName_(std::move(materialName))
{
    connect(&autosaveTimer_, &QTimer::timeout, this, &ProjectController::autosave);
}

ProjectData ProjectController::currentData() const
{
    ProjectData data;
    data.text = document_->textSpec();
    data.calibration = document_->calibrationSpec();
    data.imagePath = document_->sourcePath();
    data.transform = document_->transform();
    data.settings = document_->settings();
    data.operation = document_->operation();
    data.origin = document_->jobOrigin();
    data.materialName = materialName_ ? materialName_() : QString();
    return data;
}

// ------------------------------------------------------------------------------------ Speichern
bool ProjectController::saveTo(const QString& path)
{
    // Ein eingefügtes Bild hat keine Datei: Es kommt als PNG neben die Projektdatei (planung.md 3.13).
    if (document_->sourcePath().isEmpty() && !document_->textSpec() && !document_->calibrationSpec()) {
        const QFileInfo info(path);
        const QString imagePath = info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral("-bild.png"));
        if (!document_->image().save(imagePath)) {
            QMessageBox::warning(parentWidget_, tr("Projekt speichern"),
                                 tr("Das eingefügte Bild kann nicht gespeichert werden: %1").arg(imagePath));
            return false;
        }
        document_->setSourcePath(imagePath);
    }
    QString error;
    if (!ld::core::saveProjectFile(path, currentData(), &error)) {
        QMessageBox::warning(parentWidget_, tr("Projekt speichern"), tr("Das Projekt kann nicht gespeichert werden: %1").arg(error));
        return false;
    }
    path_ = path;
    document_->markSaved();
    AppSettings::instance().addRecentProject(QFileInfo(path).absoluteFilePath());
    AppSettings::instance().setLastFolder(FolderKind::Projects, path);
    discardRecovery();
    emit pathChanged();
    return true;
}

bool ProjectController::save()
{
    if (!document_->hasImage()) {
        QMessageBox::information(parentWidget_, tr("Projekt speichern"), tr("Es ist noch kein Bild geladen."));
        return false;
    }
    return hasPath() ? saveTo(path_) : saveAs();
}

bool ProjectController::saveAs()
{
    if (!document_->hasImage()) {
        QMessageBox::information(parentWidget_, tr("Projekt speichern"), tr("Es ist noch kein Bild geladen."));
        return false;
    }
    const QString dir = hasPath() ? QFileInfo(path_).absolutePath()
                                  : AppSettings::instance().startFolder(FolderKind::Projects);
    const QString base = hasPath() ? QFileInfo(path_).completeBaseName()
                         : document_->calibrationSpec() ? tr("kalibrierung") : document_->textSpec() ? tr("text") : document_->sourcePath().isEmpty() ? tr("auftrag")
                                                             : QFileInfo(document_->sourcePath()).completeBaseName();
    QString path = QFileDialog::getSaveFileName(parentWidget_, tr("Projekt speichern unter"),
                                                (dir.isEmpty() ? QString() : dir + QLatin1Char('/')) + base + QStringLiteral(".ldproj"),
                                                tr("Laser-Dilettant-Projekt (*.ldproj)"));
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".ldproj");
    }
    return saveTo(path);
}

bool ProjectController::maybeSave()
{
    if (!document_->hasUnsavedChanges()) {
        return true;
    }
    const QString name = hasPath() ? QFileInfo(path_).fileName()
                         : document_->calibrationSpec() ? tr("Kalibrierung") : document_->textSpec() ? tr("Text") : document_->sourcePath().isEmpty() ? tr("Unbenannt")
                                                             : QFileInfo(document_->sourcePath()).fileName();
    QMessageBox box(QMessageBox::Question, tr("Ungespeicherte Änderungen"),
                    tr("\"%1\" hat ungespeicherte Änderungen.\nSpeichern?").arg(name), QMessageBox::NoButton, parentWidget_);
    QPushButton* saveButton = box.addButton(tr("Speichern"), QMessageBox::AcceptRole);
    QPushButton* discardButton = box.addButton(tr("Verwerfen"), QMessageBox::DestructiveRole);
    box.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();
    if (box.clickedButton() == saveButton) {
        return save();
    }
    return box.clickedButton() == discardButton;
}

// ------------------------------------------------------------------------------------ Neu, Öffnen
bool ProjectController::newProject()
{
    if (!maybeSave()) {
        return false;
    }
    document_->clear();
    path_.clear();
    discardRecovery();
    emit pathChanged();
    return true;
}

bool ProjectController::openProject(const QString& pathArg)
{
    QString path = pathArg;
    if (path.isEmpty()) {
        const QString dir = AppSettings::instance().startFolder(FolderKind::Projects);
        path = QFileDialog::getOpenFileName(parentWidget_, tr("Projekt öffnen"), dir,
                                            tr("Laser-Dilettant-Projekt (*.ldproj);;Alle Dateien (*)"));
        if (path.isEmpty()) {
            return false;
        }
    }
    if (!maybeSave()) {
        return false;
    }
    return applyProject(path, Mode::Open);
}

// Lädt Projektdatei und Bild ins Dokument. recovered: Es ist eine Sicherung (gilt als ungespeichert).
bool ProjectController::applyProject(const QString& projectFile, Mode mode, bool silent)
{
    const bool recovered = mode == Mode::Recovery;
    QString error;
    QString missing;
    auto project = ld::core::loadProjectFile(projectFile, &error, &missing);
    if (!project) {
        if (silent) return false;
        QMessageBox::warning(parentWidget_, tr("Projekt öffnen"),
                             tr("Das Projekt \"%1\" kann nicht geöffnet werden:\n%2")
                                 .arg(QFileInfo(projectFile).fileName(), error));
        AppSettings::instance().removeRecentProject(QFileInfo(projectFile).absoluteFilePath());
        return false;
    }
    bool relocated = false;
    if (project->text || project->calibration) {
        // Text- oder Kalibrierungs-Auftrag: Die Grafik wird aus der Spezifikation neu gebaut.
        QImage image;
        std::shared_ptr<const ld::core::SvgModel> svg;
        if (project->calibration) {
            const ld::core::CalibrationContent content =
                ld::core::makeCalibrationContent(*project->calibration, project->settings.pixelSizeMm);
            image = content.image;
            svg = content.svg;
        } else {
            const ld::core::TextContent content = ld::core::makeTextContent(*project->text, project->settings.pixelSizeMm);
            image = content.image;
            svg = content.svg;
        }
        if (image.isNull() || !svg) {
            if (!silent) QMessageBox::warning(parentWidget_, tr("Projekt öffnen"), tr("Die Grafik im Projekt ist leer."));
            return false;
        }
        document_->setImageWithState(image, QString(), project->settings, project->transform);
        document_->setSvg(svg);
        document_->setTextSpec(project->text);
        document_->setCalibrationSpec(project->calibration);
        document_->setOperation(project->operation);
        document_->setJobOrigin(project->origin);
        if (recovered) {
            document_->markModified();
        } else {
            document_->markSaved();
        }
        if (mode == Mode::Session) {
            path_.clear();
        } else if (!recovered) {
            path_ = projectFile;
            AppSettings::instance().addRecentProject(QFileInfo(projectFile).absoluteFilePath());
            AppSettings::instance().setLastFolder(FolderKind::Projects, projectFile);
            discardRecovery();
        }
        emit pathChanged();
        return true;
    }
    if (project->imagePath.isEmpty() && silent) {
        return false;   // das Bild ist nicht mehr da: leere Seite
    }
    if (project->imagePath.isEmpty()) {
        // Das Bild fehlt: Der Bediener kann es suchen; der neue Pfad wird beim nächsten Speichern festgehalten.
        const QString text = missing.isEmpty() ? tr("Das Projekt enthält kein Bild.")
                                               : tr("Das Bild des Projekts wurde nicht gefunden:\n%1\n\nBild suchen?").arg(missing);
        if (missing.isEmpty() ||
            QMessageBox::question(parentWidget_, tr("Bild nicht gefunden"), text, QMessageBox::Yes | QMessageBox::Cancel,
                                  QMessageBox::Yes) != QMessageBox::Yes) {
            if (missing.isEmpty()) {
                QMessageBox::warning(parentWidget_, tr("Projekt öffnen"), text);
            }
            return false;
        }
        const QString found = QFileDialog::getOpenFileName(parentWidget_, tr("Bild suchen"), QFileInfo(missing).absolutePath(),
                                                           tr("Bilder (*.jpg *.jpeg *.png *.bmp *.svg);;Alle Dateien (*)"));
        if (found.isEmpty()) {
            return false;
        }
        project->imagePath = found;
        relocated = true;
    }
    const ld::core::LoadResult image = ld::core::loadImageFile(project->imagePath, project->settings.pixelSizeMm);
    if (!image.ok()) {
        if (!silent) QMessageBox::warning(parentWidget_, tr("Projekt öffnen"), image.error);
        return false;
    }

    // Bild in der Wiederherstellung gehört nicht dem Benutzer: Es bleibt nur im Speicher.
    const bool inRecovery = QFileInfo(project->imagePath).absolutePath() == QFileInfo(recoveryFolder()).absoluteFilePath();
    document_->setImageWithState(image.image, inRecovery ? QString() : project->imagePath, project->settings,
                                 project->transform);
    document_->setSvg(image.svg);
    document_->setOperation(project->operation);
    document_->setJobOrigin(project->origin);
    if (recovered || relocated) {
        document_->markModified();
    } else {
        document_->markSaved();
    }
    if (mode == Mode::Session) {
        path_.clear();   // die Sitzungsdatei ist kein Projekt des Benutzers
    } else if (!recovered) {
        path_ = projectFile;
        AppSettings::instance().addRecentProject(QFileInfo(projectFile).absoluteFilePath());
        AppSettings::instance().setLastFolder(FolderKind::Projects, projectFile);
        discardRecovery();
    }
    emit pathChanged();
    return true;
}

// ------------------------------------------------------------------------------------ Sicherung
QString ProjectController::recoveryFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/Laser-Dilettant/recovery");
}

bool ProjectController::recoveryAvailable() const
{
    return QFile::exists(QDir(recoveryFolder()).filePath(kRecoveryFile));
}

void ProjectController::discardRecovery()
{
    const QDir dir(recoveryFolder());
    QFile::remove(dir.filePath(kRecoveryFile));
    QFile::remove(dir.filePath(kRecoveryImage));
}

void ProjectController::setAutosaveMinutes(int minutes)
{
    autosaveTimer_.stop();
    if (minutes > 0) {
        autosaveTimer_.start(minutes * 60 * 1000);
    }
}

bool ProjectController::writeSnapshot(const QString& fileName, const QString& imageName)
{
    const QDir dir(recoveryFolder());
    if (!QDir().mkpath(dir.path())) {
        return false;
    }
    ProjectData data = currentData();
    if (data.imagePath.isEmpty() && !data.text && !data.calibration) {
        // Eingefügtes Bild: als PNG mitsichern (ein Text steckt als Spezifikation in der Datei).
        const QString imagePath = dir.filePath(imageName);
        if (!document_->image().save(imagePath)) {
            return false;
        }
        data.imagePath = imagePath;
    }
    const QString file = dir.filePath(fileName);
    QJsonObject json = ld::core::projectToJson(data, file);
    QJsonObject info;
    info[QStringLiteral("projectPath")] = path_;   // damit "Speichern" nach der Wiederherstellung dorthin geht
    json[QStringLiteral("recovery")] = info;
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return out.commit();
}

void ProjectController::autosave()
{
    if (!document_->hasImage() || !document_->hasUnsavedChanges()) {
        return;   // nichts Neues zu sichern
    }
    writeSnapshot(kRecoveryFile, kRecoveryImage);
}

// ------------------------------------------------------------------------------------ Letzte Sitzung
void ProjectController::saveSession()
{
    const QDir dir(recoveryFolder());
    QFile::remove(dir.filePath(kSessionFile));
    QFile::remove(dir.filePath(kSessionImage));
    if (!document_->hasImage()) {
        return;   // leere Seite merken = nichts merken
    }
    if (hasPath()) {
        // Ein gespeichertes Projekt: Nur der Pfad wird gemerkt, der Zustand steht in der Projektdatei
        // (ein beim Beenden verworfener Stand soll nicht zurückkommen).
        if (!QDir().mkpath(dir.path())) return;
        QSaveFile out(dir.filePath(kSessionFile));
        if (out.open(QIODevice::WriteOnly)) {
            QJsonObject json;
            json[QStringLiteral("sessionProject")] = QFileInfo(path_).absoluteFilePath();
            out.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
            out.commit();
        }
        return;
    }
    writeSnapshot(kSessionFile, kSessionImage);
}

bool ProjectController::restoreSession()
{
    const QString file = QDir(recoveryFolder()).filePath(kSessionFile);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QString project = json.value(QStringLiteral("sessionProject")).toString();
    if (!project.isEmpty()) {
        return QFileInfo::exists(project) && applyProject(project, Mode::Open, true);
    }
    return applyProject(file, Mode::Session, true);
}

bool ProjectController::restoreRecovery()
{
    const QString file = QDir(recoveryFolder()).filePath(kRecoveryFile);
    // Projektpfad der gesicherten Sitzung lesen, bevor die Datei gelöscht wird.
    QString originalPath;
    {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            originalPath = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("recovery")).toObject()
                               .value(QStringLiteral("projectPath")).toString();
        }
    }
    if (!applyProject(file, Mode::Recovery)) {
        discardRecovery();
        return false;
    }
    path_ = originalPath;          // leer = noch kein Projekt gespeichert
    discardRecovery();             // der Zustand steckt jetzt im Speicher
    emit pathChanged();
    return true;
}

}  // namespace ld::app
