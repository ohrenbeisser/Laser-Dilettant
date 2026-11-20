// Importdialog "Bild bearbeiten" (Meilenstein M2, siehe docs/planung.md 3.9 und 3.9a, Abschnitt 7).
//
// Links die Vorschau des Ergebnisses, rechts die Regler in Akkordeons. Alle Änderungen rechnen
// live im Hintergrund (RasterRunner). Erst "Übernehmen" schreibt Einstellungen und Größe als ein
// Undo-Schritt ins Dokument; "Abbrechen" lässt alles unverändert.
#pragma once

#include <QDialog>

#include "core/imageprocessing.h"
#include "core/imagesettings.h"
#include "core/machinesetup.h"
#include "core/objecttransform.h"
#include "core/rasterrunner.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class LengthSpinBox;
class ResolutionSpinBox;
class HistogramWidget;
class LockButton;
class PreviewView;

class ImageDialog : public QDialog {
    Q_OBJECT

public:
    // machine: aktives Maschinenprofil (für die Prüfung des Arbeitsbereichs).
    explicit ImageDialog(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                         QWidget* parent = nullptr);

    void accept() override;
    void done(int result) override;

private:
    void buildUi();
    QWidget* buildRightColumn();
    // Full und Cutting brauchen ein Schwarzweißbild: Dann gilt nur der Schwellenwert.
    bool vectorMode() const;
    bool methodLocked() const;
    ld::core::ConversionMethod effectiveMethod() const;
    void syncControlsFromSettings();   // Regler auf settings_ und transform_ stellen (ohne Signale)
    void onControlsChanged();          // Verfahren, Tonwert-Regler, Invertieren -> settings_
    void onLevelSliderChanged(int value);
    void onResult(const ld::core::ProcessResult& result);
    void requestPreview();
    void applyCropChange(const QRect& newCrop);   // Ausschnitt ändern, Größe im gleichen Maßstab
    void onWidthEdited(double mm);
    void onHeightEdited(double mm);
    void onPercentEdited(double percent);
    void afterSizeEdited();
    void updateInfo();
    void toggleCropMode(bool on);
    void cropToContent();
    void requestFullPreview();
    void resetToDefaults();

    ld::core::ImageDocument* document_;
    ld::core::MachineSetup machine_;
    ld::core::ImageSettings settings_;
    ld::core::ObjectTransform baseTransform_;   // Größe beim Öffnen des Dialogs
    ld::core::ObjectTransform transform_;       // aktuelle Größe (Breite/Höhe in mm)
    ld::core::ObjectTransform sizeAt100_;       // Größe bei 100 % für den aktuellen Ausschnitt
    QRect baseCrop_;                            // Ausschnitt beim Öffnen (immer gültig)
    QRect currentCrop_;                         // aktueller Ausschnitt (immer gültig)
    double initialPixelSizeMm_ = 0.0635;
    bool refitNext_ = true;
    ld::core::ProcessResult lastResult_;
    ld::core::RasterRunner runner_;
    ld::core::RasterRunner fullRunner_;   // das ganze Bild gerechnet, als Hintergrund im Zuschneide-Modus

    PreviewView* preview_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* busyLabel_ = nullptr;
    QPushButton* resetButton_ = nullptr;

    // Verfahren
    QComboBox* methodCombo_ = nullptr;
    QLabel* levelCaption_ = nullptr;     // "Schwelle" oder "Feinheit"
    QSlider* levelSlider_ = nullptr;
    QLabel* levelValue_ = nullptr;
    QLabel* levelHint_ = nullptr;
    QPushButton* autoButton_ = nullptr;
    QCheckBox* invertCheck_ = nullptr;
    // Histogramm und Tonwerte
    HistogramWidget* histogram_ = nullptr;
    QSlider* brightnessSlider_ = nullptr;
    QLabel* brightnessValue_ = nullptr;
    QSlider* contrastSlider_ = nullptr;
    QLabel* contrastValue_ = nullptr;
    QSlider* gammaSlider_ = nullptr;
    QLabel* gammaValue_ = nullptr;
    // Vektorisieren (nur bei Full und Cutting, siehe planung.md 3.14)
    // Graustufen-Leistung (nur beim Verfahren Graustufen-Leistung)
    QWidget* grayBox_ = nullptr;
    QSlider* grayMinSlider_ = nullptr;
    QSlider* grayMaxSlider_ = nullptr;
    QSlider* grayLevelsSlider_ = nullptr;
    QLabel* grayMinValue_ = nullptr;
    QLabel* grayMaxValue_ = nullptr;
    QLabel* grayLevelsValue_ = nullptr;
    QWidget* vectorSection_ = nullptr;
    QWidget* svgSection_ = nullptr;
    QCheckBox* fullStrokesCheck_ = nullptr;
    QCheckBox* cutStrokesCheck_ = nullptr;
    QCheckBox* cutFillsCheck_ = nullptr;
    QLabel* svgTextHint_ = nullptr;
    QSlider* turdSlider_ = nullptr;
    QSlider* cornerSlider_ = nullptr;
    QSlider* simplifySlider_ = nullptr;
    QLabel* turdValue_ = nullptr;
    QLabel* cornerValue_ = nullptr;
    QLabel* simplifyValue_ = nullptr;
    QLabel* methodHint_ = nullptr;
    QSlider* sharpenSlider_ = nullptr;
    QLabel* sharpenValue_ = nullptr;
    // Zuschneiden
    QPushButton* cropButton_ = nullptr;
    QPushButton* cropResetButton_ = nullptr;
    QPushButton* cropContentButton_ = nullptr;
    QLabel* cropLabel_ = nullptr;
    // Skalieren
    LengthSpinBox* widthSpin_ = nullptr;
    LengthSpinBox* heightSpin_ = nullptr;
    LockButton* lockButton_ = nullptr;
    QDoubleSpinBox* percentSpin_ = nullptr;
    ResolutionSpinBox* pixelSizeSpin_ = nullptr;   // Linien/mm, dpi oder Pixelgröße
    QLabel* pixelSizeLabel_ = nullptr;
    // Info
    QLabel* infoLabel_ = nullptr;
};

}  // namespace ld::app
