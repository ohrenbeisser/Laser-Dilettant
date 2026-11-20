// Dialog "Materialtest" (siehe docs/planung.md 3.20): Tabelle aus Feldern mit einem Motiv. Die Power steigt von
// Spalte zu Spalte, die Speed von Zeile zu Zeile; beide Werte stehen als einzeln änderbare Felder über und links
// der Tabelle. Links die Tabelle, rechts Auswahl, Beschriftung, Übersicht und Knöpfe.
// Ein Klick auf ein Feld wählt es aus (für "Als Material speichern"). Der Dialog ist nicht modal,
// damit der Not-Aus des Hauptfensters bedienbar bleibt. Gebrannt wird vom Hauptfenster aus.
#pragma once

#include <QDialog>
#include <QList>
#include <QTimer>

#include "core/engraving.h"
#include "core/machinesetup.h"
#include "core/materialtest.h"

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;

namespace ld::app {

class ResolutionSpinBox;
class SpeedSpinBox;

class MaterialTestDialog : public QDialog {
    Q_OBJECT

public:
    // operation: die aktuellen Gravur-Werte (für die Vorbelegung); pixelSizeMm: Rasterabstand des Lasers.
    MaterialTestDialog(const ld::core::MachineSetup& machine, const ld::core::Operation& operation,
                       ld::core::JobOrigin origin, double pixelSizeMm, QWidget* parent = nullptr);

    // Verbindung und Zustand des Lasers: Start und Rahmen gehen nur, wenn er verbunden und bereit ist.
    void setRunState(bool connected, bool idle);
    // Die aktuelle Spezifikation (für Start, Rahmen und G-Code speichern).
    ld::core::MaterialTestSpec spec() const;

signals:
    void startRequested(const ld::core::MaterialTestSpec& spec);
    void frameRequested(const ld::core::MaterialTestSpec& spec);
    void saveGcodeRequested(const ld::core::MaterialTestSpec& spec);
    // Das gewählte Feld soll als Material gespeichert werden (Werte des Feldes, Modus des Tests).
    void saveMaterialRequested(double speedMmPerMin, double powerPercent, ld::core::EngraveMode mode);

private:
    void buildUi();
    void updateResolutionEnabled();        // Auflösung im Cutting ausgrauen
    // Zahl der Felder geändert: Eingabefelder und Felder neu anlegen. keepEnds: erster und letzter Wert bleiben.
    void rebuildTable(bool keepEnds = true);
    void updateTiles();                    // Motiv oder Größe geändert: Symbole der Felder
    void applyDefaults(bool keepEnds);     // Werte vorbelegen (gleichmäßig verteilt)
    void spread(bool speeds);              // Werte gleichmäßig zwischen dem ersten und dem letzten verteilen
    void scheduleSummary();
    void updateSummary();
    void updateButtons();
    QString speedLabel(double mmPerMin) const;
    bool cutGroup() const;                 // gehört der gewählte Modus zur Gruppe "Schneiden"?
    int selectedColumn() const;
    int selectedRow() const;

    ld::core::MachineSetup machine_;
    ld::core::Operation operation_;
    ld::core::JobOrigin origin_;
    double pixelSizeMm_;

    double startSpeed_ = 600.0, endSpeed_ = 1200.0;   // Vorbelegung (erster und letzter Wert)
    double startPower_ = 20.0, endPower_ = 100.0;
    int cells_ = 5;
    ld::core::TestMotif motif_ = ld::core::TestMotif::SquareFilled;
    double sizeMm_ = 10.0;
    ld::core::EngraveMode mode_ = ld::core::EngraveMode::Line;

    QComboBox* cellsCombo_ = nullptr;
    QComboBox* sizeCombo_ = nullptr;
    QButtonGroup* modeGroup_ = nullptr;
    ResolutionSpinBox* resolutionSpin_ = nullptr;
    QLabel* resolutionLabel_ = nullptr;
    QComboBox* motifCombo_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QList<SpeedSpinBox*> speedSpins_;
    QList<QDoubleSpinBox*> powerSpins_;
    QList<QToolButton*> tiles_;            // zeilenweise
    QButtonGroup* tileGroup_ = nullptr;
    SpeedSpinBox* labelSpeed_ = nullptr;
    QDoubleSpinBox* labelPower_ = nullptr;
    QLabel* summary_ = nullptr;            // Hinweis zur Power
    QLabel* sizeLabel_ = nullptr;
    QLabel* linesLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    QLabel* warning_ = nullptr;
    QPushButton* frameButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* saveGcodeButton_ = nullptr;
    QPushButton* saveMaterialButton_ = nullptr;
    QTimer summaryTimer_;
    bool rebuilding_ = false;
    bool connected_ = false;
    bool idle_ = false;
    bool fits_ = true;
};

}  // namespace ld::app
