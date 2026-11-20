// Das geladene Bild samt Einstellungen, Größe, Drehung und Spiegelung, mit Rückgängig/Wiederholen.
//
// Zurzeit gibt es höchstens ein Bild (siehe planung.md 3.8). Das Original (QImage) bleibt
// unverändert; Größe, Drehung und Spiegelung stehen in einer ObjectTransform, die Bildbearbeitung
// (Schwelle, Dithering, Ausschnitt ...) in ImageSettings. Aus beiden berechnet das Dokument im
// Hintergrund das Raster, das die Arbeitsfläche zeigt (und später den G-Code speist).
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QUndoStack>

#include "core/engraving.h"
#include "core/gcode.h"
#include "core/imageprocessing.h"
#include "core/imagesettings.h"
#include "core/objecttransform.h"
#include "core/rasterrunner.h"
#include "core/svgmodel.h"
#include "core/calibration.h"
#include "core/textmodel.h"
#include <optional>
#include <memory>

namespace ld::core {

class ImageDocument : public QObject {
    Q_OBJECT

public:
    explicit ImageDocument(QObject* parent = nullptr);
    // Beim Zerstören meldet das Dokument nichts mehr: Der Undo-Stapel würde sonst "sauber" melden und damit
    // Empfänger aufrufen (zum Beispiel das Hauptfenster), die schon halb abgebaut sind.
    ~ImageDocument() override;

    bool hasImage() const { return !image_.isNull(); }
    const QImage& image() const { return image_; }
    // Vektordaten, wenn das Bild eine SVG-Datei ist (sonst leer). Nach setImage/setImageWithState setzen.
    std::shared_ptr<const SvgModel> svg() const { return svg_; }
    void setSvg(std::shared_ptr<const SvgModel> svg);
    // Ist das Bild ein Text (aus dem Dialog "Schrift")? Dann steht hier seine Spezifikation (für Bearbeiten und
    // Projekt); das Bild selbst hat keine Datei. Nach setImage/setImageWithState setzen.
    const std::optional<TextSpec>& textSpec() const { return text_; }
    void setTextSpec(std::optional<TextSpec> spec) { text_ = std::move(spec); }
    // Ist das Bild die Kalibrierungsgrafik (planung.md 3.25)? Sie wird ohne Korrektur des Maschinenprofils gebrannt.
    const std::optional<CalibrationSpec>& calibrationSpec() const { return calibration_; }
    void setCalibrationSpec(std::optional<CalibrationSpec> spec) { calibration_ = std::move(spec); }
    const QString& sourcePath() const { return sourcePath_; }  // leer bei Zwischenablage
    const ObjectTransform& transform() const { return transform_; }
    const ImageSettings& settings() const { return settings_; }

    // Das berechnete Arbeitsraster (Graubild, siehe imageprocessing.h). Leer, solange die
    // Rechnung läuft und noch kein Ergebnis da ist.
    const QImage& raster() const { return raster_; }
    bool isRasterBusy() const { return runner_.isBusy(); }

    // Gravur-Einstellungen (Speed, Leistung, Richtung ...) und Job-Ursprung. Sie gehören nicht zum
    // Undo-Stapel (siehe planung.md 3.10); das spätere Projekt speichert sie mit.
    const Operation& operation() const { return operation_; }
    void setOperation(const Operation& operation);
    JobOrigin jobOrigin() const { return jobOrigin_; }
    void setJobOrigin(JobOrigin origin);

    // Auftrag für den G-Code-Erzeuger aus dem aktuellen Zustand (Raster, Größe, Operation, Ursprung).
    // Das Raster kann leer sein, solange es noch berechnet wird (isRasterBusy()).
    GcodeJob makeGcodeJob(const MachineSetup& machine) const;

    // Rückgängig/Wiederholen. Die Oberfläche hängt ihre Menüeinträge hier an.
    QUndoStack* undoStack() { return &undoStack_; }

    // Wurde seit dem Laden etwas geändert (Größe, Drehen, Spiegeln, Bildbearbeitung)? Gilt nur für das
    // Bild selbst (Undo-Stapel); die Rückfrage beim Ersetzen des Bildes benutzt das.
    bool isModified() const { return !undoStack_.isClean(); }

    // Ungespeicherte Änderungen am Projekt: zusätzlich zum Undo-Stapel zählen Gravur-Werte,
    // Material und Job-Ursprung (siehe planung.md 3.13).
    bool hasUnsavedChanges() const { return !undoStack_.isClean() || extraDirty_; }
    void markModified();   // etwas Außerhalb des Undo-Stapels hat sich geändert
    void markSaved();      // gespeichert (oder frisch geladen): alles gilt als unverändert

    // Ersetzt das Bild. Die Startgröße ist Bildpixel * mmPerPixel, die Pixelgröße der
    // Einstellungen ebenfalls. Leert den Undo-Stapel. Ein leeres Bild wird ignoriert (false).
    bool setImage(const QImage& image, const QString& sourcePath, double mmPerPixel);

    // Setzt Bild und gespeicherten Zustand (Projekt öffnen): Einstellungen und Größe kommen aus dem
    // Projekt, der Undo-Stapel wird geleert, alles gilt als unverändert.
    bool setImageWithState(const QImage& image, const QString& sourcePath, const ImageSettings& settings,
                           const ObjectTransform& transform);
    // Entfernt das Bild (Neu). Gravur-Werte und Job-Ursprung bleiben erhalten.
    void clear();
    // Pfad des Bildes ändern (zum Beispiel nach dem Speichern eines eingefügten Bildes als Datei).
    void setSourcePath(const QString& path) { sourcePath_ = path; }

    // Änderungen; jede ist ein Undo-Schritt, aber nur, wenn sich wirklich etwas ändert.
    void setWidthMm(double widthMm, bool keepAspect);
    void setHeightMm(double heightMm, bool keepAspect);
    // Breite und Höhe zugleich (zum Beispiel beim Ziehen an den Anfassern der Arbeitsfläche).
    void setSizeMm(double widthMm, double heightMm);
    // Lage der Grafik zum Nullpunkt (mm, Sicht des Bedieners, Y nach unten); ein Undo-Schritt (planung.md 3.24).
    void setOffsetMm(const QPointF& offsetMm);
    // Verschieben mit der Maus: Während des Ziehens wirkt previewOffsetMm sofort (ohne Undo-Schritt); beim Loslassen
    // macht commitOffsetMm daraus EINEN Undo-Schritt vom Startwert (startOffsetMm) zum jetzigen Wert.
    void previewOffsetMm(const QPointF& offsetMm);
    void commitOffsetMm(const QPointF& startOffsetMm);
    void setRotationDeg(double degrees);
    void flipHorizontal();
    void flipVertical();
    void setPixelSizeMm(double mm);

    // Übernimmt das Ergebnis des Importdialogs (Einstellungen und Größe) als ein Undo-Schritt.
    void applyEdit(const ImageSettings& settings, const ObjectTransform& transform);

signals:
    void imageReplaced();      // neues Bild geladen
    void transformChanged();   // Größe, Drehung, Spiegelung oder Einstellungen geändert (auch durch Undo/Redo)
    void settingsChanged();    // Einstellungen geändert
    void rasterChanged();      // neues Arbeitsraster berechnet
    void rasterStarted();      // eine Berechnung des Rasters hat begonnen
    void engravingChanged();   // Operation oder Job-Ursprung geändert
    void modifiedChanged();    // hasUnsavedChanges() hat sich geändert

private:
    friend class StateCommand;

    struct State {
        ObjectTransform transform;
        ImageSettings settings;
        friend bool operator==(const State&, const State&) = default;
    };

    void changeState(const State& next, const QString& text);
    void applyState(const State& state);  // nur für StateCommand
    void refreshRaster(bool force);
    void onRasterFinished(const ProcessResult& result);

    QImage image_;
    std::shared_ptr<const SvgModel> svg_;
    std::optional<TextSpec> text_;
    std::optional<CalibrationSpec> calibration_;
    bool lastUsedSvg_ = false;
    QString sourcePath_;
    ObjectTransform transform_;
    ImageSettings settings_;
    Operation operation_;
    JobOrigin jobOrigin_ = JobOrigin::TopLeft;
    QImage raster_;
    RasterRunner runner_;
    State lastRequested_;
    bool hasRequested_ = false;
    QUndoStack undoStack_;
    bool extraDirty_ = false;
};

}  // namespace ld::core
