#include "core/imagedocument.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUndoCommand>

namespace ld::core {

// Ein Undo-Schritt: wechselt zwischen zwei Zuständen (Transformation und Einstellungen).
// QUndoStack ruft redo() beim Einreihen sofort auf, danach undo()/redo() bei Strg+Z / Strg+Y.
class StateCommand : public QUndoCommand {
public:
    StateCommand(ImageDocument* doc, const ImageDocument::State& before, const ImageDocument::State& after,
                 const QString& text)
        : QUndoCommand(text), doc_(doc), before_(before), after_(after)
    {
    }
    void undo() override { doc_->applyState(before_); }
    void redo() override { doc_->applyState(after_); }

private:
    ImageDocument* doc_;
    ImageDocument::State before_;
    ImageDocument::State after_;
};

ImageDocument::ImageDocument(QObject* parent) : QObject(parent), runner_(80)
{
    connect(&runner_, &RasterRunner::finished, this,
            [this](const ProcessResult& result) { onRasterFinished(result); });
    connect(&runner_, &RasterRunner::started, this, &ImageDocument::rasterStarted);
    // Der Undo-Stapel meldet, wenn er "sauber" wird oder nicht mehr ist.
    connect(&undoStack_, &QUndoStack::cleanChanged, this, [this] { emit modifiedChanged(); });
}

ImageDocument::~ImageDocument()
{
    blockSignals(true);
    undoStack_.blockSignals(true);   // der Undo-Stapel wird nach diesem Destruktor aufgeräumt und würde cleanChanged melden
}

void ImageDocument::markModified()
{
    if (!extraDirty_) {
        extraDirty_ = true;
        emit modifiedChanged();
    }
}

void ImageDocument::markSaved()
{
    const bool wasDirty = hasUnsavedChanges();
    extraDirty_ = false;
    undoStack_.setClean();
    if (wasDirty) {
        emit modifiedChanged();
    }
}

bool ImageDocument::setImageWithState(const QImage& image, const QString& sourcePath,
                                      const ImageSettings& settings, const ObjectTransform& transform)
{
    if (image.isNull()) {
        return false;
    }
    undoStack_.clear();
    svg_.reset();
    text_.reset();
    calibration_.reset();
    image_ = image;
    sourcePath_ = sourcePath;
    transform_ = transform;
    settings_ = settings;
    raster_ = QImage();
    markSaved();
    emit imageReplaced();
    emit transformChanged();
    emit settingsChanged();
    emit rasterChanged();
    refreshRaster(true);
    return true;
}

void ImageDocument::clear()
{
    undoStack_.clear();
    svg_.reset();
    text_.reset();
    calibration_.reset();
    image_ = QImage();
    sourcePath_.clear();
    transform_ = ObjectTransform{};
    settings_ = ImageSettings{};
    raster_ = QImage();
    runner_.cancel();
    hasRequested_ = false;
    markSaved();
    emit imageReplaced();
    emit transformChanged();
    emit settingsChanged();
    emit rasterChanged();
}

bool ImageDocument::setImage(const QImage& image, const QString& sourcePath, double mmPerPixel)
{
    if (image.isNull()) {
        return false;
    }
    undoStack_.clear();  // Alte Schritte gehören zum alten Bild.
    svg_.reset();
    text_.reset();
    calibration_.reset();
    image_ = image;
    sourcePath_ = sourcePath;
    transform_ = initialTransform(image.size(), mmPerPixel);
    settings_ = ImageSettings{};
    settings_.pixelSizeMm = mmPerPixel;
    raster_ = QImage();
    markSaved();
    emit imageReplaced();
    emit transformChanged();
    emit settingsChanged();
    emit rasterChanged();  // das alte Raster ist weg
    refreshRaster(true);
    return true;
}

void ImageDocument::changeState(const State& next, const QString& text)
{
    if (!hasImage() || next == State{transform_, settings_}) {
        return;
    }
    undoStack_.push(new StateCommand(this, State{transform_, settings_}, next, text));
}

void ImageDocument::applyState(const State& state)
{
    const bool settingsDiffer = !(state.settings == settings_);
    transform_ = state.transform;
    settings_ = state.settings;
    emit transformChanged();
    if (settingsDiffer) {
        emit settingsChanged();
    }
    refreshRaster(false);
}

void ImageDocument::refreshRaster(bool force)
{
    if (!hasImage()) {
        runner_.cancel();
        return;
    }
    // Das Raster hängt nur von Größe und Einstellungen ab, nicht von Drehung oder Spiegelung.
    // Full und Cutting arbeiten bei Rasterbildern mit jeder Methode (Schwellenwert über potrace, Dithering und
    // Graustufen mit pixelgenauen Flächen, siehe planung.md 3.18). Nur bei SVG-Dateien kommt das Raster aus den
    // Vektordaten und ist schwarzweiß.
    ImageSettings effective = settings_;
    if (svg_ && operation_.mode != EngraveMode::Line) {
        effective.method = ConversionMethod::Threshold;
    }
    effective.vectorize = VectorizeSettings{};   // wirkt erst beim G-Code, nicht auf das Raster
    effective.grayPower = GrayPowerSettings{};   // ebenso: die Leistungsabbildung gehört zum G-Code
    ImageDocument::State key{transform_, effective};
    key.transform.rotationDeg = 0.0;
    key.transform.flipHorizontal = false;
    key.transform.flipVertical = false;
    key.transform.offsetXMm = 0.0;   // die Lage zum Nullpunkt ändert das Raster nicht
    key.transform.offsetYMm = 0.0;
    // Full mit SVG: Das Raster kommt aus den Vektordaten statt aus dem Rasterbild.
    const bool useSvg = svg_ && operation_.mode == EngraveMode::Full;
    if (!force && hasRequested_ && key == lastRequested_ && useSvg == lastUsedSvg_) {
        return;
    }
    lastRequested_ = key;
    lastUsedSvg_ = useSvg;
    hasRequested_ = true;
    runner_.request(image_, effective, QSizeF(transform_.widthMm, transform_.heightMm),
                    useSvg ? svg_ : nullptr);
}

void ImageDocument::onRasterFinished(const ProcessResult& result)
{
    raster_ = result.raster;
    emit rasterChanged();
}

void ImageDocument::setWidthMm(double widthMm, bool keepAspect)
{
    State next{transform_, settings_};
    next.transform = withWidth(transform_, widthMm, keepAspect);
    changeState(next, QCoreApplication::translate("ld::core", "Breite ändern"));
}

void ImageDocument::setHeightMm(double heightMm, bool keepAspect)
{
    State next{transform_, settings_};
    next.transform = withHeight(transform_, heightMm, keepAspect);
    changeState(next, QCoreApplication::translate("ld::core", "Höhe ändern"));
}

void ImageDocument::setSizeMm(double widthMm, double heightMm)
{
    if (widthMm <= 0.0 || heightMm <= 0.0) {
        return;
    }
    State next{transform_, settings_};
    next.transform.widthMm = widthMm;
    next.transform.heightMm = heightMm;
    changeState(next, QCoreApplication::translate("ld::core", "Größe ändern"));
}

void ImageDocument::setOffsetMm(const QPointF& offsetMm)
{
    State next{transform_, settings_};
    next.transform.offsetXMm = offsetMm.x();
    next.transform.offsetYMm = offsetMm.y();
    changeState(next, QCoreApplication::translate("ld::core", "Grafik verschieben"));
}

void ImageDocument::previewOffsetMm(const QPointF& offsetMm)
{
    if (!hasImage() || (transform_.offsetXMm == offsetMm.x() && transform_.offsetYMm == offsetMm.y())) {
        return;
    }
    transform_.offsetXMm = offsetMm.x();
    transform_.offsetYMm = offsetMm.y();
    emit transformChanged();
}

void ImageDocument::commitOffsetMm(const QPointF& startOffsetMm)
{
    if (!hasImage()) {
        return;
    }
    State before{transform_, settings_};
    before.transform.offsetXMm = startOffsetMm.x();
    before.transform.offsetYMm = startOffsetMm.y();
    const State after{transform_, settings_};
    if (before == after) {
        return;
    }
    // Der Stapel führt redo() aus (setzt den Zustand erneut, ohne Änderung); danach ist der Schritt rückgängig zu machen.
    undoStack_.push(new StateCommand(this, before, after, QCoreApplication::translate("ld::core", "Grafik verschieben")));
}

void ImageDocument::setRotationDeg(double degrees)
{
    State next{transform_, settings_};
    next.transform.rotationDeg = degrees;
    changeState(next, QCoreApplication::translate("ld::core", "Drehen"));
}

void ImageDocument::flipHorizontal()
{
    State next{transform_, settings_};
    next.transform.flipHorizontal = !next.transform.flipHorizontal;
    changeState(next, QCoreApplication::translate("ld::core", "Horizontal spiegeln"));
}

void ImageDocument::flipVertical()
{
    State next{transform_, settings_};
    next.transform.flipVertical = !next.transform.flipVertical;
    changeState(next, QCoreApplication::translate("ld::core", "Vertikal spiegeln"));
}

void ImageDocument::setPixelSizeMm(double mm)
{
    if (mm <= 0.0) {
        return;
    }
    State next{transform_, settings_};
    next.settings.pixelSizeMm = mm;
    changeState(next, QCoreApplication::translate("ld::core", "Pixelgröße ändern"));
}

void ImageDocument::setSvg(std::shared_ptr<const SvgModel> svg)
{
    svg_ = std::move(svg);
    if (hasImage()) {
        refreshRaster(true);
    }
}

void ImageDocument::setOperation(const Operation& operation)
{
    if (operation == operation_) {
        return;
    }
    const bool modeChanged = (operation.mode == EngraveMode::Line) != (operation_.mode == EngraveMode::Line);
    operation_ = operation;
    markModified();
    if (modeChanged || (svg_ && operation.mode == EngraveMode::Full)) {
        refreshRaster(false);   // Full/Cutting brauchen ein Schwarzweiß-Raster
    }
    emit engravingChanged();
}

void ImageDocument::setJobOrigin(JobOrigin origin)
{
    origin = normalizedJobOrigin(origin);
    if (origin == jobOrigin_) {
        return;
    }
    jobOrigin_ = origin;
    markModified();
    emit engravingChanged();
}

GcodeJob ImageDocument::makeGcodeJob(const MachineSetup& machine) const
{
    GcodeJob job;
    job.raster = raster_;
    job.transform = transform_;
    job.operation = operation_;
    job.origin = jobOrigin_;
    job.machine = machine;
    job.pixelSizeMm = settings_.pixelSizeMm;
    job.vectorize = settings_.vectorize;
    job.svg = svg_;
    job.svgSettings = settings_.svg;
    if (calibration_) {
        // Die Messgrafik besteht nur aus Linien und wird immer ohne Korrektur gebrannt (sonst wäre die Messung verfälscht).
        job.svgSettings.cutStrokes = true;
        job.svgSettings.cutFills = false;
        job.machine = withoutCorrection(job.machine);
    }
    if (text_) {
        // Ein Text besteht nur aus Füllungen: Cutting fährt immer deren Umrisse (die SVG-Wahl gilt für Dateien).
        job.svgSettings.cutFills = true;
        job.svgSettings.cutStrokes = false;
    }
    job.grayPowerEnabled = settings_.method == ConversionMethod::Grayscale;
    job.pixelAreas = settings_.method != ConversionMethod::Threshold && !svg_;
    job.grayPower = settings_.grayPower;
    job.title = sourcePath_.isEmpty() ? QCoreApplication::translate("ld::core", "Zwischenablage")
                                      : QFileInfo(sourcePath_).fileName();
    return job;
}

void ImageDocument::applyEdit(const ImageSettings& settings, const ObjectTransform& transform)
{
    changeState(State{transform, settings}, QCoreApplication::translate("ld::core", "Bild bearbeiten"));
}

}  // namespace ld::core
