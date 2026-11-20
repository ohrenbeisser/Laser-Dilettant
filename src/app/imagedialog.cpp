#include "imagedialog.h"
#include "helpwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "accordion.h"
#include "appsettings.h"
#include "displayutil.h"
#include "lockbutton.h"
#include "unitspinboxes.h"
#include "core/imagedocument.h"
#include "core/units.h"
#include "previewview.h"

namespace ld::app {

using ld::core::ConversionMethod;

namespace {
constexpr int kMaxDitherCell = 8;
constexpr double kZoomStep = 1.25;

bool isDither(ConversionMethod method)
{
    return method == ConversionMethod::FloydSteinberg || method == ConversionMethod::Atkinson ||
           method == ConversionMethod::Bayer;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// Histogramm der Grauwerte mit Linie bei der Schwelle.
// ---------------------------------------------------------------------------------------------
class HistogramWidget : public QWidget {
public:
    explicit HistogramWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(70);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    // Histogramm des Originalbildes: wird hellgrau hinter dem aktuellen gezeichnet.
    void setOriginal(const ld::core::Histogram& histogram)
    {
        original_ = histogram;
        update();
    }

    // marker < 0: keine Linie.
    void setData(const ld::core::Histogram& histogram, int marker)
    {
        histogram_ = histogram;
        marker_ = marker;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Base));
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        // Das Original (hellgrau) liegt hinten; jede Kurve ist auf ihr eigenes Maximum bezogen.
        int originalMax = 0;
        for (int c : original_) {
            originalMax = std::max(originalMax, c);
        }
        if (originalMax > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xb4, 0xb4, 0xb4, 110));
            const double binWidth = static_cast<double>(width() - 2) / 256.0;
            for (int i = 0; i < 256; ++i) {
                const double h = std::sqrt(static_cast<double>(original_[static_cast<size_t>(i)]) / originalMax) *
                                 (height() - 4);
                p.drawRect(QRectF(1 + i * binWidth, height() - 2 - h, std::max(1.0, binWidth), h));
            }
        }

        int maxCount = 0;
        for (int c : histogram_) {
            maxCount = std::max(maxCount, c);
        }
        if (maxCount > 0) {
            // Wurzel-Skala, damit auch kleine Häufigkeiten sichtbar bleiben.
            QColor bar = palette().color(QPalette::Text);
            bar.setAlpha(170);
            p.setPen(Qt::NoPen);
            p.setBrush(bar);
            const double binWidth = static_cast<double>(width() - 2) / 256.0;
            for (int i = 0; i < 256; ++i) {
                const double h = std::sqrt(static_cast<double>(histogram_[static_cast<size_t>(i)]) / maxCount) *
                                 (height() - 4);
                p.drawRect(QRectF(1 + i * binWidth, height() - 2 - h, std::max(1.0, binWidth), h));
            }
        }
        if (marker_ >= 0) {
            p.setPen(QPen(QColor(0xd4, 0x2a, 0x29), 2));
            const double x = 1 + marker_ * (static_cast<double>(width() - 2) / 256.0);
            p.drawLine(QPointF(x, 1), QPointF(x, height() - 2));
        }
    }

private:
    ld::core::Histogram histogram_{};
    ld::core::Histogram original_{};
    int marker_ = -1;
};

namespace {

// Zeile "Regler  Wert" mit eigener Beschriftung als QLabel (die Beschriftung kann wechseln).
QSlider* addSliderRow(QFormLayout* form, QWidget* caption, int minimum, int maximum, QLabel** valueLabel)
{
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(minimum, maximum);
    auto* value = new QLabel;
    value->setMinimumWidth(46);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(value);
    form->addRow(caption, row);
    *valueLabel = value;
    return slider;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
ImageDialog::ImageDialog(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine, QWidget* parent)
    : QDialog(parent), document_(document), machine_(machine), runner_(150), fullRunner_(100)
{
    // Titel: Dateiname des Bildes (ein eingefügtes Bild hat keinen) und Name des Dialogs.
    const QString fileName = document_->sourcePath().isEmpty() ? tr("Eingefügtes Bild")
                                                               : QFileInfo(document_->sourcePath()).fileName();
    setWindowTitle(tr("%1 – Bild bearbeiten").arg(fileName));
    attachHelp(this, ld::core::helptopic::kImageEdit);   // F1 und Knopf "Hilfe" (Handbuch)
    // Titelleiste mit Minimieren, Maximieren/Wiederherstellen und Schließen (planung.md 3.9a). Als normales
    // Fenster (nicht als "Dialog") angefordert, denn viele Fensterverwaltungen blenden bei Dialogen die
    // Knöpfe zum Minimieren und Maximieren aus. Modal bleibt der Dialog trotzdem.
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowCloseButtonHint);
    setSizeGripEnabled(true);
    setMinimumSize(900, 560);
    resize(1060, 760);
    // Gemerkte Größe/Lage (auch "maximiert") wiederherstellen.
    restoreGeometry(QSettings().value(QStringLiteral("imageDialog/geometry")).toByteArray());

    settings_ = document_->settings();
    baseTransform_ = document_->transform();
    transform_ = baseTransform_;
    sizeAt100_ = baseTransform_;
    baseCrop_ = ld::core::effectiveCrop(settings_, document_->image().size());
    currentCrop_ = baseCrop_;
    initialPixelSizeMm_ = settings_.pixelSizeMm;

    buildUi();
    preview_->setSourceImage(document_->image());
    syncControlsFromSettings();
    updateInfo();

    connect(&runner_, &ld::core::RasterRunner::started, this, [this] { busyLabel_->setText(tr("Berechne …")); });
    connect(&runner_, &ld::core::RasterRunner::finished, this,
            [this](const ld::core::ProcessResult& result) { onResult(result); });
    connect(&fullRunner_, &ld::core::RasterRunner::finished, this, [this](const ld::core::ProcessResult& result) {
        if (cropButton_->isChecked()) {
            preview_->setCropBackdrop(result.raster, document_->image().size());
        }
    });
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, [this] {
        pixelSizeLabel_->setText(resolutionLabel());
        syncControlsFromSettings();
        updateInfo();
    });
    requestPreview();
}

void ImageDialog::buildUi()
{
    auto* main = new QVBoxLayout(this);

    // ------------------------------------------------------------ Mitte: Vorschau und Regler
    auto* body = new QHBoxLayout;
    preview_ = new PreviewView;
    body->addWidget(preview_, 1);
    body->addWidget(buildRightColumn());
    main->addLayout(body, 1);

    // ------------------------------------------------------------ untere Reihe
    auto* bottom = new QHBoxLayout;
    auto* zoomOut = new QPushButton(QStringLiteral("−"));
    auto* zoomIn = new QPushButton(QStringLiteral("+"));
    zoomOut->setFixedWidth(34);
    zoomIn->setFixedWidth(34);
    zoomOut->setToolTip(tr("Verkleinern (Taste −)"));
    zoomIn->setToolTip(tr("Vergrößern (Taste +)"));
    auto* fitButton = new QPushButton(tr("Einpassen"));
    auto* oneToOneButton = new QPushButton(tr("1:1"));
    zoomLabel_ = new QLabel;
    busyLabel_ = new QLabel;
    resetButton_ = new QPushButton(tr("Zurücksetzen"));
    auto* cancelButton = new QPushButton(tr("Abbrechen"));
    auto* okButton = new QPushButton(tr("Übernehmen"));
    okButton->setDefault(true);

    bottom->addWidget(zoomOut);
    bottom->addWidget(zoomIn);
    bottom->addWidget(fitButton);
    bottom->addWidget(oneToOneButton);
    bottom->addWidget(zoomLabel_);
    bottom->addSpacing(12);
    bottom->addWidget(busyLabel_);
    bottom->addStretch();
    bottom->addWidget(resetButton_);
    bottom->addSpacing(18);  // Zurücksetzen etwas abgesetzt von Abbrechen/Übernehmen
    bottom->addWidget(cancelButton);
    bottom->addWidget(okButton);
    main->addLayout(bottom);

    connect(zoomOut, &QPushButton::clicked, this, [this] { preview_->zoomBy(1.0 / kZoomStep); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { preview_->zoomBy(kZoomStep); });
    connect(fitButton, &QPushButton::clicked, preview_, &PreviewView::fit);
    connect(oneToOneButton, &QPushButton::clicked, preview_, &PreviewView::showOneToOne);
    connect(preview_, &PreviewView::zoomChanged, this, [this] {
        zoomLabel_->setText(tr("Zoom %1 %").arg(std::lround(preview_->zoomPercent())));
    });
    connect(resetButton_, &QPushButton::clicked, this, &ImageDialog::resetToDefaults);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);

    // Tasten + und − zoomen ebenfalls (in Eingabefeldern tippt man trotzdem ganz normal).
    auto* plusKey = new QShortcut(QKeySequence(Qt::Key_Plus), this);
    connect(plusKey, &QShortcut::activated, this, [this] { preview_->zoomBy(kZoomStep); });
    auto* minusKey = new QShortcut(QKeySequence(Qt::Key_Minus), this);
    connect(minusKey, &QShortcut::activated, this, [this] { preview_->zoomBy(1.0 / kZoomStep); });
}

QWidget* ImageDialog::buildRightColumn()
{
    auto* content = new QWidget;
    auto* list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);

    // ---------------------------------------------------------------- Verfahren
    auto* method = new AccordionSection(QStringLiteral("imageDialog/method"), tr("Verfahren"), true);
    {
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        methodCombo_ = new QComboBox;
        methodCombo_->addItem(tr("Schwellenwert"), static_cast<int>(ConversionMethod::Threshold));
        methodCombo_->addItem(tr("Floyd-Steinberg"), static_cast<int>(ConversionMethod::FloydSteinberg));
        methodCombo_->addItem(tr("Atkinson"), static_cast<int>(ConversionMethod::Atkinson));
        methodCombo_->addItem(tr("Bayer"), static_cast<int>(ConversionMethod::Bayer));
        methodCombo_->addItem(tr("Graustufen-Leistung"), static_cast<int>(ConversionMethod::Grayscale));
        form->addRow(tr("Verfahren"), methodCombo_);
        methodHint_ = new QLabel(tr("Bei SVG-Dateien arbeiten Full und Cutting mit Schwarzweiß: Es gilt nur der Schwellenwert."));
        methodHint_->setWordWrap(true);
        methodHint_->setEnabled(false);  // gedämpfte Schrift
        form->addRow(QString(), methodHint_);

        // Ein Slider für zwei Dinge: Schwelle (beim Schwellenwert) oder Feinheit (beim Dithering).
        levelCaption_ = new QLabel(tr("Schwelle"));
        levelSlider_ = addSliderRow(form, levelCaption_, 0, 255, &levelValue_);
        levelHint_ = new QLabel;
        levelHint_->setWordWrap(true);
        levelHint_->setEnabled(false);  // gedämpfte Schrift
        form->addRow(QString(), levelHint_);
        autoButton_ = new QPushButton(tr("Auto (Otsu)"));
        form->addRow(QString(), autoButton_);
        invertCheck_ = new QCheckBox(tr("Invertieren"));
        form->addRow(QString(), invertCheck_);

        // Abbildung der Graustufen auf Laserleistung (nur bei "Graustufen-Leistung").
        grayBox_ = new QWidget;
        auto* grayForm = new QFormLayout(grayBox_);
        grayForm->setContentsMargins(0, 0, 0, 0);
        grayMinSlider_ = addSliderRow(grayForm, new QLabel(tr("Mindestleistung")), 0, 100, &grayMinValue_);
        grayMaxSlider_ = addSliderRow(grayForm, new QLabel(tr("Höchstleistung")), 1, 100, &grayMaxValue_);
        grayLevelsSlider_ = addSliderRow(grayForm, new QLabel(tr("Stufen")), 8, 64, &grayLevelsValue_);
        grayMinSlider_->setToolTip(tr("Leistung für fast weiße Punkte in % der Gravur-Leistung (reines Weiß bleibt aus). "
                                      "Diodenlaser markieren erst ab etwa 10 % sichtbar."));
        grayMaxSlider_->setToolTip(tr("Leistung für Schwarz in % der Gravur-Leistung"));
        grayLevelsSlider_->setToolTip(tr("Die Leistung wird auf so viele Stufen gerundet; weniger Stufen ergeben kleineren G-Code"));
        form->addRow(grayBox_);
        method->contentLayout()->addLayout(form);
    }
    list->addWidget(method);

    // ---------------------------------------------------------------- Vektorisieren (nur Full/Cutting)
    {
        auto* vector = new AccordionSection(QStringLiteral("imageDialog/vector"), tr("Vektorisieren"), true);
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        turdSlider_ = addSliderRow(form, new QLabel(tr("Flecken ignorieren")), 0, 50, &turdValue_);
        cornerSlider_ = addSliderRow(form, new QLabel(tr("Eckenschärfe")), 0, 133, &cornerValue_);
        simplifySlider_ = addSliderRow(form, new QLabel(tr("Kurven vereinfachen")), 0, 200, &simplifyValue_);
        turdSlider_->setToolTip(tr("Flecken bis zu so vielen Pixeln werden weggelassen"));
        cornerSlider_->setToolTip(tr("Links: alles wird zur Ecke, rechts: alles wird rund"));
        simplifySlider_->setToolTip(tr("Größer: weniger, dafür gröbere Kurvenstücke"));
        vector->contentLayout()->addLayout(form);
        vectorSection_ = vector;
        list->addWidget(vector);
    }

    // ---------------------------------------------------------------- SVG (nur bei SVG-Dateien, Full/Cutting)
    {
        auto* svg = new AccordionSection(QStringLiteral("imageDialog/svg"), tr("SVG"), true);
        fullStrokesCheck_ = new QCheckBox(tr("Striche zusätzlich als Fläche (in Strichbreite)"));
        cutStrokesCheck_ = new QCheckBox(tr("Striche als Mittellinie"));
        cutFillsCheck_ = new QCheckBox(tr("Umrisse der Füllungen"));
        svgTextHint_ = new QLabel(tr("Die Datei enthält Text. Full und Cutting lesen keinen Text; "
                                     "bitte den Text vorher in Pfade umwandeln."));
        svgTextHint_->setWordWrap(true);
        svg->contentLayout()->addWidget(fullStrokesCheck_);
        svg->contentLayout()->addWidget(cutStrokesCheck_);
        svg->contentLayout()->addWidget(cutFillsCheck_);
        svg->contentLayout()->addWidget(svgTextHint_);
        svgSection_ = svg;
        list->addWidget(svg);
    }

    // ---------------------------------------------------------------- Histogramm und Tonwerte
    auto* histogramSection = new AccordionSection(QStringLiteral("imageDialog/histogram"), tr("Histogramm"), true);
    {
        histogram_ = new HistogramWidget;
        histogramSection->contentLayout()->addWidget(histogram_);
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        brightnessSlider_ = addSliderRow(form, new QLabel(tr("Helligkeit")), -100, 100, &brightnessValue_);
        contrastSlider_ = addSliderRow(form, new QLabel(tr("Kontrast")), -100, 100, &contrastValue_);
        gammaSlider_ = addSliderRow(form, new QLabel(tr("Gamma")), 20, 300, &gammaValue_);
        sharpenSlider_ = addSliderRow(form, new QLabel(tr("Schärfen")), 0, 100, &sharpenValue_);
        histogramSection->contentLayout()->addLayout(form);
    }
    list->addWidget(histogramSection);

    // ---------------------------------------------------------------- Zuschneiden
    auto* crop = new AccordionSection(QStringLiteral("imageDialog/crop"), tr("Zuschneiden"), false);
    {
        cropButton_ = new QPushButton(tr("Bereich wählen …"));
        cropButton_->setCheckable(true);
        cropResetButton_ = new QPushButton(tr("Ganzes Bild"));
        auto* row = new QHBoxLayout;
        row->addWidget(cropButton_, 1);
        row->addWidget(cropResetButton_);
        crop->contentLayout()->addLayout(row);
        // Setzt den Rahmen genau um alles, was der Laser mit den aktuellen Einstellungen brennt (ohne Rand).
        cropContentButton_ = new QPushButton(tr("Auf Inhalt zuschneiden"));
        cropContentButton_->setToolTip(tr("Legt den Rahmen um alles, was der Laser brennt: Der leere Rand fällt weg.\n"
                                          "Danach kannst du den Rahmen noch anpassen und mit \"Bereich übernehmen\" bestätigen."));
        crop->contentLayout()->addWidget(cropContentButton_);
        cropLabel_ = new QLabel;
        crop->contentLayout()->addWidget(cropLabel_);
    }
    list->addWidget(crop);
    // Bei SVG gibt es kein Zuschneiden: Der Abschnitt bleibt sichtbar, ist aber ausgegraut.
    crop->setEnabled(!document_->svg());
    if (document_->svg()) {
        crop->setToolTip(tr("Bei SVG-Dateien gibt es kein Zuschneiden."));
    }

    // ---------------------------------------------------------------- Skalieren
    auto* scale = new AccordionSection(QStringLiteral("imageDialog/scale"), tr("Skalieren"), true);
    {
        widthSpin_ = new LengthSpinBox(0.01, 5000.0, 1, 3, 1.0);
        heightSpin_ = new LengthSpinBox(0.01, 5000.0, 1, 3, 1.0);
        lockButton_ = new LockButton;
        percentSpin_ = new QDoubleSpinBox;
        percentSpin_->setRange(1.0, 1000.0);
        percentSpin_->setDecimals(1);
        percentSpin_->setSingleStep(5.0);
        percentSpin_->setSuffix(tr(" %"));
        percentSpin_->setKeyboardTracking(false);
        pixelSizeSpin_ = new ResolutionSpinBox(0.01, 1.0);
        pixelSizeLabel_ = new QLabel(resolutionLabel());

        auto* grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(2);   // Breite und Höhe rücken zusammen
        grid->addWidget(new QLabel(tr("Breite")), 0, 0);
        grid->addWidget(widthSpin_, 0, 1);
        grid->addWidget(new QLabel(tr("Höhe")), 1, 0);
        grid->addWidget(heightSpin_, 1, 1);
        grid->addWidget(lockButton_, 0, 2, 2, 1);   // Klammer über beide Zeilen
        grid->setRowMinimumHeight(2, 6);
        grid->addWidget(new QLabel(tr("Skalieren")), 3, 0);
        grid->addWidget(percentSpin_, 3, 1);
        grid->addWidget(pixelSizeLabel_, 4, 0);
        grid->addWidget(pixelSizeSpin_, 4, 1);
        grid->setColumnStretch(1, 1);
        scale->contentLayout()->addLayout(grid);
    }
    list->addWidget(scale);

    // ---------------------------------------------------------------- Info
    auto* info = new AccordionSection(QStringLiteral("imageDialog/info"), tr("Info"), true);
    {
        infoLabel_ = new QLabel;
        infoLabel_->setWordWrap(true);
        infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        info->contentLayout()->addWidget(infoLabel_);
    }
    list->addStretch();   // der Info-Abschnitt steht am unteren Rand der Spalte
    list->addWidget(info);

    // Die Spalte scrollt, falls der Platz nicht reicht.
    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(360);

    // ---------------------------------------------------------------- Verbindungen
    connect(methodCombo_, &QComboBox::currentIndexChanged, this, &ImageDialog::onControlsChanged);
    for (QCheckBox* check : {fullStrokesCheck_, cutStrokesCheck_, cutFillsCheck_}) {
        connect(check, &QCheckBox::toggled, this, &ImageDialog::onControlsChanged);
    }
    for (QSlider* slider : {grayMinSlider_, grayMaxSlider_, grayLevelsSlider_}) {
        connect(slider, &QSlider::valueChanged, this, &ImageDialog::onControlsChanged);
    }
    for (QSlider* slider : {turdSlider_, cornerSlider_, simplifySlider_}) {
        connect(slider, &QSlider::valueChanged, this, &ImageDialog::onControlsChanged);
    }
    for (QSlider* slider : {brightnessSlider_, contrastSlider_, gammaSlider_, sharpenSlider_}) {
        connect(slider, &QSlider::valueChanged, this, &ImageDialog::onControlsChanged);
    }
    connect(levelSlider_, &QSlider::valueChanged, this, &ImageDialog::onLevelSliderChanged);
    connect(autoButton_, &QPushButton::clicked, this, [this] {
        settings_.autoThreshold = true;
        syncControlsFromSettings();
        requestPreview();
    });
    connect(invertCheck_, &QCheckBox::toggled, this, &ImageDialog::onControlsChanged);

    connect(widthSpin_, &LengthSpinBox::mmChanged, this, &ImageDialog::onWidthEdited);
    connect(heightSpin_, &LengthSpinBox::mmChanged, this, &ImageDialog::onHeightEdited);
    connect(percentSpin_, &QDoubleSpinBox::valueChanged, this, &ImageDialog::onPercentEdited);
    connect(pixelSizeSpin_, &ResolutionSpinBox::mmChanged, this, [this](double value) {
        settings_.pixelSizeMm = value;
        syncControlsFromSettings();  // die Punktgröße in mm hängt von der Pixelgröße ab
        updateInfo();
        requestPreview();
    });

    connect(cropButton_, &QPushButton::toggled, this, &ImageDialog::toggleCropMode);
    connect(cropContentButton_, &QPushButton::clicked, this, &ImageDialog::cropToContent);
    connect(cropResetButton_, &QPushButton::clicked, this, [this] {
        const QRect full(QPoint(0, 0), document_->image().size());
        if (preview_->cropMode()) {
            preview_->setCropRect(full);
        } else {
            applyCropChange(QRect());
            refitNext_ = true;
            syncControlsFromSettings();
            requestPreview();
        }
    });
    connect(preview_, &PreviewView::cropChanged, this, [this](const QRect& frame) {
        if (preview_->cropMode()) {
            cropLabel_->setText(tr("Rahmen: %1 × %2 Pixel").arg(frame.width()).arg(frame.height()));
        }
    });
    return scroll;
}

// Regler anhand von settings_ und transform_ einstellen, ohne dass sie Änderungen melden.
void ImageDialog::syncControlsFromSettings()
{
    const QSignalBlocker b1(methodCombo_), b2(levelSlider_), b3(brightnessSlider_), b4(contrastSlider_),
        b5(gammaSlider_), b6(sharpenSlider_), b7(invertCheck_), b8(percentSpin_), b9(turdSlider_),
        b10(cornerSlider_), b11(simplifySlider_);

    const ConversionMethod method = effectiveMethod();
    {
        const QSignalBlocker g1(grayMinSlider_), g2(grayMaxSlider_), g3(grayLevelsSlider_);
        grayMinSlider_->setValue(settings_.grayPower.minPercent);
        grayMaxSlider_->setValue(settings_.grayPower.maxPercent);
        grayLevelsSlider_->setValue(settings_.grayPower.levels);
        grayMinValue_->setText(tr("%1 %").arg(settings_.grayPower.minPercent));
        grayMaxValue_->setText(tr("%1 %").arg(settings_.grayPower.maxPercent));
        grayLevelsValue_->setText(QString::number(settings_.grayPower.levels));
        grayBox_->setVisible(method == ConversionMethod::Grayscale);
    }
    methodCombo_->setCurrentIndex(methodCombo_->findData(static_cast<int>(method)));
    // Gesperrt nur bei SVG mit Full oder Cutting: Dort kommen die Flächen aus den Vektordaten (Schwarzweiß).
    methodCombo_->setEnabled(!methodLocked());
    methodCombo_->setToolTip(methodLocked() ? tr("Bei SVG-Dateien gilt mit Full und Cutting immer der Schwellenwert (nur Schwarz und Weiß).")
                                            : QString());
    methodHint_->setVisible(methodLocked());
    {
        using ld::core::EngraveMode;
        const bool hasSvg = static_cast<bool>(document_->svg());
        const EngraveMode mode = document_->operation().mode;
        const QSignalBlocker c1(fullStrokesCheck_), c2(cutStrokesCheck_), c3(cutFillsCheck_);
        fullStrokesCheck_->setChecked(settings_.svg.fullStrokes);
        cutStrokesCheck_->setChecked(settings_.svg.cutStrokes);
        cutFillsCheck_->setChecked(settings_.svg.cutFills);
        svgSection_->setVisible(hasSvg && vectorMode() && !document_->textSpec() && !document_->calibrationSpec());
        fullStrokesCheck_->setVisible(mode == EngraveMode::Full);
        cutStrokesCheck_->setVisible(mode == EngraveMode::Cutting);
        cutFillsCheck_->setVisible(mode == EngraveMode::Cutting);
        svgTextHint_->setVisible(hasSvg && document_->svg()->hasText);
        // Bei SVG-Cutting gehen die Striche direkt aus den Vektordaten: potrace wird nicht gebraucht.
        // potrace wird nur beim Schwellenwert gebraucht (Dithering und Graustufen haben pixelgenaue Flächen).
        vectorSection_->setVisible(vectorMode() && effectiveMethod() == ConversionMethod::Threshold &&
                                   !(hasSvg && mode == EngraveMode::Cutting));
    }
    turdSlider_->setValue(settings_.vectorize.turdSize);
    cornerSlider_->setValue(static_cast<int>(std::lround(settings_.vectorize.alphaMax * 100.0)));
    simplifySlider_->setValue(static_cast<int>(std::lround(settings_.vectorize.optTolerance * 100.0)));
    turdValue_->setText(tr("%1 px").arg(settings_.vectorize.turdSize));
    cornerValue_->setText(QString::number(settings_.vectorize.alphaMax, 'f', 2));
    simplifyValue_->setText(QString::number(settings_.vectorize.optTolerance, 'f', 2));
    const bool threshold = method == ConversionMethod::Threshold;
    const bool dither = isDither(method);

    // Der Slider ist Schwelle (0..255) oder Feinheit (1..8), je nach Verfahren.
    if (dither) {
        levelCaption_->setText(tr("Feinheit"));
        levelSlider_->setRange(1, kMaxDitherCell);
        levelSlider_->setValue(settings_.ditherCell);
        levelValue_->setText(tr("%1 px").arg(settings_.ditherCell));
        levelHint_->setText(tr("Punktgröße %1 · links fein, rechts grob")
                                .arg(formatLength(settings_.ditherCell * settings_.pixelSizeMm, 3)));
    } else {
        levelCaption_->setText(tr("Schwelle"));
        levelSlider_->setRange(0, 255);
        const int shown = settings_.autoThreshold ? lastResult_.usedThreshold : settings_.threshold;
        levelSlider_->setValue(shown);
        levelValue_->setText(QString::number(shown));
    }
    levelSlider_->setEnabled(threshold || dither);
    levelHint_->setVisible(dither);
    autoButton_->setEnabled(threshold && !settings_.autoThreshold);

    brightnessSlider_->setValue(settings_.brightness);
    contrastSlider_->setValue(settings_.contrast);
    gammaSlider_->setValue(static_cast<int>(std::lround(settings_.gamma * 100.0)));
    sharpenSlider_->setValue(settings_.sharpen);
    invertCheck_->setChecked(settings_.invert);
    brightnessValue_->setText(QString::number(settings_.brightness));
    contrastValue_->setText(QString::number(settings_.contrast));
    gammaValue_->setText(QString::number(settings_.gamma, 'f', 2));
    sharpenValue_->setText(QString::number(settings_.sharpen) + QStringLiteral(" %"));

    // Größe: Breite, Höhe und Prozent (Prozent bezieht sich auf die Breite).
    widthSpin_->setMm(transform_.widthMm);
    heightSpin_->setMm(transform_.heightMm);
    percentSpin_->setValue(sizeAt100_.widthMm > 0.0 ? transform_.widthMm / sizeAt100_.widthMm * 100.0 : 100.0);
    pixelSizeSpin_->setMm(settings_.pixelSizeMm);

    const QRect crop = ld::core::effectiveCrop(settings_, document_->image().size());
    if (!preview_->cropMode()) {
        cropLabel_->setText(settings_.crop.isEmpty()
                                ? tr("ganzes Bild (%1 × %2 Pixel)").arg(crop.width()).arg(crop.height())
                                : tr("Ausschnitt: %1 × %2 Pixel").arg(crop.width()).arg(crop.height()));
    }
    histogram_->setData(lastResult_.histogram, threshold ? levelSlider_->value() : -1);
}

void ImageDialog::onControlsChanged()
{
    // Der Slider "Schwelle/Feinheit" meldet sich getrennt (onLevelSliderChanged).
    if (!methodLocked()) {
        settings_.method = static_cast<ConversionMethod>(methodCombo_->currentData().toInt());
    }
    settings_.svg.fullStrokes = fullStrokesCheck_->isChecked();
    settings_.svg.cutStrokes = cutStrokesCheck_->isChecked();
    settings_.svg.cutFills = cutFillsCheck_->isChecked();
    settings_.grayPower.minPercent = grayMinSlider_->value();
    settings_.grayPower.maxPercent = grayMaxSlider_->value();
    // Die Mindestleistung liegt nie über der Höchstleistung.
    settings_.grayPower.minPercent = std::min(settings_.grayPower.minPercent, settings_.grayPower.maxPercent);
    settings_.grayPower.levels = grayLevelsSlider_->value();
    settings_.vectorize.turdSize = turdSlider_->value();
    settings_.vectorize.alphaMax = cornerSlider_->value() / 100.0;
    settings_.vectorize.optTolerance = simplifySlider_->value() / 100.0;
    settings_.brightness = brightnessSlider_->value();
    settings_.contrast = contrastSlider_->value();
    settings_.gamma = gammaSlider_->value() / 100.0;
    settings_.sharpen = sharpenSlider_->value();
    settings_.invert = invertCheck_->isChecked();
    syncControlsFromSettings();
    updateInfo();
    requestPreview();
}

void ImageDialog::onLevelSliderChanged(int value)
{
    if (isDither(effectiveMethod())) {
        settings_.ditherCell = std::clamp(value, 1, kMaxDitherCell);
    } else if (effectiveMethod() == ConversionMethod::Threshold) {
        settings_.autoThreshold = false;  // von Hand eingestellt
        settings_.threshold = value;
    }
    syncControlsFromSettings();
    requestPreview();
}

void ImageDialog::requestPreview()
{
    ld::core::ImageSettings effective = settings_;
    effective.method = effectiveMethod();
    const bool useSvg = document_->svg() && document_->operation().mode == ld::core::EngraveMode::Full;
    runner_.request(document_->image(), effective, QSizeF(transform_.widthMm, transform_.heightMm),
                    useSvg ? document_->svg() : nullptr);
    requestFullPreview();
}

// Im Zuschneide-Modus zeigt die Vorschau das gerechnete Ergebnis des ganzen Bildes (nicht das Original).
// Gerechnet wird im selben Maßstab wie die normale Vorschau; die Szene bleibt in Pixeln des Originals.
void ImageDialog::requestFullPreview()
{
    if (!cropButton_->isChecked()) {
        return;
    }
    ld::core::ImageSettings effective = settings_;
    effective.method = effectiveMethod();
    effective.crop = QRect();
    const QSize image = document_->image().size();
    const QRect crop = ld::core::effectiveCrop(settings_, image);
    const double mmPerPixel = crop.width() > 0 ? transform_.widthMm / crop.width() : effective.pixelSizeMm;
    fullRunner_.request(document_->image(), effective, QSizeF(image.width() * mmPerPixel, image.height() * mmPerPixel));
}

// Das Verfahren ist nur bei SVG-Dateien mit Full oder Cutting festgelegt (Schwellenwert).
bool ImageDialog::methodLocked() const
{
    return vectorMode() && document_->svg();
}

bool ImageDialog::vectorMode() const
{
    return document_->operation().mode != ld::core::EngraveMode::Line;
}

ConversionMethod ImageDialog::effectiveMethod() const
{
    return methodLocked() ? ConversionMethod::Threshold : settings_.method;
}

void ImageDialog::onResult(const ld::core::ProcessResult& result)
{
    lastResult_ = result;
    histogram_->setOriginal(result.originalHistogram);   // hellgrau im Hintergrund
    busyLabel_->clear();
    if (settings_.autoThreshold || effectiveMethod() == ConversionMethod::Threshold) {
        syncControlsFromSettings();   // Schwelle (Otsu) und Histogramm-Linie anzeigen
    } else {
        histogram_->setData(result.histogram, -1);
    }
    preview_->setResult(result.raster, QSizeF(transform_.widthMm, transform_.heightMm), refitNext_);
    refitNext_ = false;
    updateInfo();
}

// Ausschnitt ändern: Der Maßstab (mm pro Quellpixel) bleibt gleich, die Größe ändert sich
// entsprechend (planung.md 3.9). Eine eingestellte Skalierung bleibt dabei erhalten.
void ImageDialog::applyCropChange(const QRect& newCrop)
{
    settings_.crop = newCrop;
    const QRect crop = ld::core::effectiveCrop(settings_, document_->image().size());
    if (currentCrop_.width() > 0 && currentCrop_.height() > 0) {
        transform_.widthMm *= static_cast<double>(crop.width()) / currentCrop_.width();
        transform_.heightMm *= static_cast<double>(crop.height()) / currentCrop_.height();
    }
    currentCrop_ = crop;
    sizeAt100_ = ld::core::transformForCrop(baseTransform_, baseCrop_, crop, 100.0);
    updateInfo();
}

void ImageDialog::onWidthEdited(double mm)
{
    if (transform_.widthMm > 0.0 && lockButton_->isChecked()) {
        transform_.heightMm *= mm / transform_.widthMm;  // Schloss zu: Seitenverhältnis bleibt
    }
    transform_.widthMm = mm;
    afterSizeEdited();
}

void ImageDialog::onHeightEdited(double mm)
{
    if (transform_.heightMm > 0.0 && lockButton_->isChecked()) {
        transform_.widthMm *= mm / transform_.heightMm;
    }
    transform_.heightMm = mm;
    afterSizeEdited();
}

void ImageDialog::onPercentEdited(double percent)
{
    // Prozent skaliert immer gleichmäßig: das gerade eingestellte Seitenverhältnis bleibt.
    const double newWidth = sizeAt100_.widthMm * percent / 100.0;
    if (transform_.widthMm > 0.0) {
        transform_.heightMm *= newWidth / transform_.widthMm;
    }
    transform_.widthMm = newWidth;
    afterSizeEdited();
}

void ImageDialog::afterSizeEdited()
{
    refitNext_ = true;  // das Bild soll nach der Größenänderung wieder ganz sichtbar sein
    syncControlsFromSettings();
    updateInfo();
    requestPreview();
}

void ImageDialog::updateInfo()
{
    const QSize raster = ld::core::rasterSizeFor(QSizeF(transform_.widthMm, transform_.heightMm),
                                                 settings_.pixelSizeMm);
    const QRect crop = ld::core::effectiveCrop(settings_, document_->image().size());
    QString text = tr("Größe: %1 × %2\nArbeitsbild: %3 × %4 Pixel (%5 dpi)")
                       .arg(formatLength(transform_.widthMm), formatLength(transform_.heightMm))
                       .arg(raster.width())
                       .arg(raster.height())
                       .arg(std::lround(ld::core::dpiFromMmPerPixel(settings_.pixelSizeMm)));

    if (!raster.isEmpty() && crop.width() > 0) {
        const double ratio = static_cast<double>(raster.width()) / crop.width();
        if (ratio > 1.5) {
            text += tr("\n⚠ Das Bild hat weniger Pixel als das Arbeitsbild und wird hochgerechnet (Faktor %L1).")
                        .arg(ratio, 0, 'f', 1);
        } else if (ratio < 0.5) {
            text += tr("\nHinweis: Das Bild ist viel feiner als der Laser und wird verkleinert.");
        }
    }
    if (!ld::core::fitsWorkArea(ld::core::boundingSize(transform_), machine_)) {
        const QSizeF area = ld::core::workAreaInOperatorView(machine_);
        text += tr("\n⚠ Größer als der Arbeitsbereich (%1 × %2).")
                    .arg(formatLength(area.width(), 0), formatLength(area.height(), 0));
    }
    if (lastResult_.clamped) {
        text += tr("\n⚠ Das Arbeitsbild wurde auf 64 Megapixel begrenzt.");
    }
    infoLabel_->setText(text);
}

// "Auf Inhalt zuschneiden": Rahmen um alles, was mit den aktuellen Einstellungen gebrannt wird.
void ImageDialog::cropToContent()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QRect content = ld::core::contentBounds(document_->image(), settings_,
                                                  methodLocked());
    QApplication::restoreOverrideCursor();
    if (content.isEmpty()) {
        QMessageBox::information(this, tr("Auf Inhalt zuschneiden"),
                                 tr("Mit den aktuellen Einstellungen wird nichts gebrannt, es gibt keinen Inhalt zum Zuschneiden."));
        return;
    }
    if (!cropButton_->isChecked()) {
        cropButton_->setChecked(true);   // schaltet den Zuschneide-Modus ein (Rahmen bearbeiten)
    }
    preview_->setCropRect(content);
}

void ImageDialog::toggleCropMode(bool on)
{
    if (on) {
        cropButton_->setText(tr("Bereich übernehmen"));
        preview_->setCropMode(true, settings_.crop);
        requestFullPreview();   // bis das Ergebnis da ist, bleibt das Original zu sehen
        return;
    }
    fullRunner_.cancel();
    // Rahmen übernehmen: Ist er das ganze Bild, gilt "kein Ausschnitt".
    cropButton_->setText(tr("Bereich wählen …"));
    const QRect frame = preview_->cropRect();
    preview_->setCropMode(false);
    const QRect full(QPoint(0, 0), document_->image().size());
    applyCropChange((frame == full || frame.isEmpty()) ? QRect() : frame);
    refitNext_ = true;
    syncControlsFromSettings();
    requestPreview();
}

void ImageDialog::resetToDefaults()
{
    if (cropButton_->isChecked()) {
        const QSignalBlocker blocker(cropButton_);
        cropButton_->setChecked(false);
        cropButton_->setText(tr("Bereich wählen …"));
        preview_->setCropMode(false);
    }
    settings_ = ld::core::ImageSettings{};
    settings_.pixelSizeMm = initialPixelSizeMm_;
    applyCropChange(QRect());
    transform_ = sizeAt100_;  // 100 %
    lockButton_->setChecked(true);
    refitNext_ = true;
    syncControlsFromSettings();
    updateInfo();
    requestPreview();
}

void ImageDialog::accept()
{
    // Ist der Zuschneide-Modus noch an, den Rahmen vorher übernehmen.
    if (cropButton_->isChecked()) {
        cropButton_->setChecked(false);
    }
    document_->applyEdit(settings_, transform_);
    QDialog::accept();
}

void ImageDialog::done(int result)
{
    // Größe und Lage für das nächste Öffnen merken (auch bei Abbrechen und Schließen).
    QSettings().setValue(QStringLiteral("imageDialog/geometry"), saveGeometry());
    QDialog::done(result);
}

}  // namespace ld::app
