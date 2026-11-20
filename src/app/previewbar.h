// Vorschau-Leiste: sitzt oberhalb der Arbeitsfläche zwischen linkem und rechtem Panel.
// Die Vorschau zeigt immer Bild und Laserwege zusammen. Die Leiste enthält die Ebenen der
// Laserwege (Gravur, Leerfahrt, Eilgang), das Tempo, den Knopf "Simulation" und eine Infozeile.
// Früher war das ein Abschnitt im rechten Panel (Änderung vom 2026-11-18, planung.md 3.11).
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace ld::app {

class PreviewBar : public QWidget {
    Q_OBJECT

public:
    explicit PreviewBar(QWidget* parent = nullptr);

    bool showBurn() const;
    bool showTravel() const;
    bool showRapid() const;
    double simulationSpeed() const;
    // Fortschritt der Simulation 0..1: Der Knopf füllt sich von links nach rechts.
    void setSimulationProgress(double fraction);
    // Zustand der Simulation: canStart = Knopf "Simulation" bedienbar (auch zum Stoppen),
    // running = läuft gerade, paused = angehalten.
    void setSimulationState(bool enabled, bool running, bool paused);

signals:
    void pathLayersChanged();
    void simulationSpeedChanged(double speed);
    void simulationClicked();        // Knopf "Simulation" bzw. "Stopp"
    void simulationPauseClicked();   // Knopf "Pause" bzw. "Weiter"
    void infoRequested();            // Knopf "Info"
    void zoomInRequested();          // Knopf "Zoom +"
    void zoomOutRequested();         // Knopf "Zoom -"
    void fitRequested();             // Knopf "Einpassen"

protected:
    // Die Symbole der Zoom-Knöpfe sind gezeichnet und müssen beim Wechsel Hell/Dunkel neu entstehen.
    void changeEvent(QEvent* event) override;

private:
    QCheckBox* burnCheck_ = nullptr;
    QCheckBox* travelCheck_ = nullptr;
    QCheckBox* rapidCheck_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
    class ProgressButton;
    ProgressButton* simButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    bool simRunning_ = false;   // die Simulation läuft (bestimmt die Beschriftung des Knopfs)
    QPushButton* infoButton_ = nullptr;
    QPushButton* zoomInButton_ = nullptr;
    QPushButton* zoomOutButton_ = nullptr;
    QPushButton* fitButton_ = nullptr;
    void updateZoomIcons();
};

}  // namespace ld::app
