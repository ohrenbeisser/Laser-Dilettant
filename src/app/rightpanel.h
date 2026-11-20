// Rechtes Panel: Datei, Gravur, Größe, Qualität, Rahmen und ein fester Fuß mit
// Start / Pause / Rahmen (immer sichtbar, scrollt nicht mit).
// Aufbau wie im Mockup (docs/mockup/oberflaeche.html).
// Meilenstein M0: Nur die Oberfläche, die Bedienelemente haben noch keine Funktion.
#pragma once

#include <QList>
#include <QTimer>
#include <QWidget>

#include "core/machinesetup.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;
class QToolButton;
class QSlider;
class QSpinBox;

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class LengthSpinBox;
class LockButton;
class ResolutionSpinBox;
class SpeedSpinBox;

class RightPanel : public QWidget {
    Q_OBJECT

public:
    // Das Panel zeigt und ändert Größe, Drehung und Spiegelung des Bildes im Dokument.
    explicit RightPanel(ld::core::ImageDocument* document, QWidget* parent = nullptr);

    // Pixelgröße aus "Qualität" in mm (Startgröße beim nächsten Laden, siehe planung.md 3.8).
    double pixelSizeMm() const;

    // Vorgabe-Pixelgröße des Maschinenprofils (aus der Laserauflösung). Wirkt nur, solange noch
    // kein Bild geladen ist; ein geladenes Bild behält seine Pixelgröße.
    void setDefaultPixelSizeMm(double mm);
    // Das aktive Maschinenprofil: für den Überscan-Richtwert (Beschleunigung, Aufstellung).
    void setMachine(const ld::core::MachineSetup& machine);

    // ---- Vorschau (M5, siehe planung.md 3.11) ----

    // Start und Pause im festen Fuß.
    QPushButton* startButton() const { return startButton_; }
    QPushButton* pauseButton() const { return pauseButton_; }
    QPushButton* testSizeButton() const { return testSizeButton_; }
    // Live-Regelung: Während eines Auftrags wirken die Regler Speed und Leistung aus dem Abschnitt
    // Gravur als Prozent-Regler (Speed und Leistung 10..200 % des Startwerts, Leistung höchstens 100 % der Maschine).
    void beginLiveControl();
    void endLiveControl();
    // Während eines echten Auftrags: alles sperren, was den Auftrag ändern würde (nur Speed und
    // Leistung bleiben als Live-Regler bedienbar, dazu Start/Pause).
    void setEditingLocked(bool locked);
    // NC-Auftrag (planung.md 3.21): Speed und Power zeigen Prozent (100 % = wie in der Datei) und wirken immer live; alles
    // Bildbezogene ist ausgegraut. 
    void setNcMode(bool on);
    bool ncMode() const { return ncMode_; }
    void setLiveValues(int speedPercent, int powerPercent);
    // Startwerte des laufenden Auftrags (= 100 %), für die Fernansicht: Speed in mm/min, Leistung in Prozent der Maschine.
    bool liveActive() const { return liveMode_; }
    // Die Regler selbst (Wert und Bereich, wie sie im Programm stehen): Speed in mm/min (im NC-Modus Prozent), Leistung in Prozent.
    int speedSliderValue() const;
    int speedSliderMin() const;
    int speedSliderMax() const;
    int powerSliderValue() const;
    int powerSliderMin() const;
    int powerSliderMax() const;
    int liveBaseSpeedMmPerMin() const { return liveBaseSpeed_; }
    int liveBasePowerPercent() const { return liveBasePower_; }
    // Rahmen: Speed und Leistung aus dem Abschnitt "Rahmen".
    double frameSpeedMmPerMin() const;
    double framePowerPercent() const;
    void setFrameValues(double speedMmPerMin, double powerPercent);

    bool keepAspect() const;

signals:
    void keepAspectChanged(bool keep);
    void liveSpeedChanged(int percent);
    void livePowerChanged(int percent);
    void loadImageRequested();
    void editImageRequested();
    void textRequested();
    // Der Speed-Wert war höher als die Grenze des Maschinenprofils und wurde auf sie gesetzt (mm/min).
    void speedLimited(double maxMmPerMin);
    void clearRequested();   // Knopf "Leeren": Arbeitsfläche leeren, Projekt schließen

private:
    // Zeigt die Werte des Dokuments in den Feldern (ohne dabei Änderungen auszulösen).
    void refreshFromDocument();
    void updateOverscanHint();
    void updateSpeedLabel();
    // Höchster Wert des Speed-Reglers: die maximale Speed des Maschinenprofils.
    int speedMax() const;
    void enforceSpeedLimit();
    // Blendet die Felder ein/aus, die zur gewählten Gravurart gehören (Linie, Full, Cutting).
    void updateModeVisibility();

    ld::core::ImageDocument* document_;
    LengthSpinBox* widthSpin_ = nullptr;
    LengthSpinBox* heightSpin_ = nullptr;
    LengthSpinBox* positionXSpin_ = nullptr;   // Lage der Grafik zum Nullpunkt, in Zählrichtung der Lineale
    LengthSpinBox* positionYSpin_ = nullptr;
    QToolButton* zeroButton_ = nullptr;        // Nullpunkt setzen: Versatz 0
    QDoubleSpinBox* rotationSpin_ = nullptr;
    LockButton* lockButton_ = nullptr;   // Schloss: geschlossen = Seitenverhältnis bleibt
    QPushButton* flipHorizontalButton_ = nullptr;
    QPushButton* flipVerticalButton_ = nullptr;
    ResolutionSpinBox* pixelSizeSpin_ = nullptr;   // Auflösung: Linien/mm, dpi oder Pixelgröße (Einstellungen)
    QLabel* pixelSizeLabel_ = nullptr;
    QWidget* frameSection_ = nullptr;             // Abschnitt Rahmen: bei laufendem Auftrag gesperrt
    QPushButton* editImageButton_ = nullptr;

    // Gravur (gehören zur Operation im Dokument, siehe planung.md 3.10)
    QPushButton* fullButton_ = nullptr;
    QPushButton* cuttingButton_ = nullptr;
    QPushButton* lineButton_ = nullptr;
    QSlider* angleSlider_ = nullptr;
    QSlider* speedSlider_ = nullptr;
    QSlider* powerSlider_ = nullptr;
    QSpinBox* passesSpin_ = nullptr;
    LengthSpinBox* overscanSpin_ = nullptr;
    QLabel* overscanHint_ = nullptr;
    QWidget* angleBox_ = nullptr;
    QList<QWidget*> lockable_;   // Bereiche, die beim Lauf gesperrt werden
    QFormLayout* advancedForm_ = nullptr;
    LengthSpinBox* toleranceSpin_ = nullptr;
    QLabel* speedValueLabel_ = nullptr;
    ld::core::MachineSetup machine_;
    QCheckBox* bidirectionalCheck_ = nullptr;

    // Vorschau
    QPushButton* startButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* testSizeButton_ = nullptr;
    static int livePercent(int value, int base);
    bool ncMode_ = false;         // NC-Auftrag statt Bild
    bool locked_ = false;         // ein echter Auftrag läuft: Eingaben gesperrt
    QWidget* fileSection_ = nullptr;
    void applyLockStates();
    bool liveMode_ = false;       // true während eines Auftrags: Regler steuern GRBL statt das Dokument
    int liveBaseSpeed_ = 0;       // Werte beim Start = 100 %
    int liveBasePower_ = 0;
    QTimer liveTimer_;
    SpeedSpinBox* frameSpeedSpin_ = nullptr;
    QDoubleSpinBox* framePowerSpin_ = nullptr;
    bool refreshing_ = false;   // true, solange die Felder aus dem Dokument gefüllt werden
};

}  // namespace ld::app
