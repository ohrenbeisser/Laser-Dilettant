// Dialog "Kalibrierung" (siehe docs/planung.md 3.25): Eine Messgrafik (Winkel mit Teilstrichen und Kreis) wird in die
// Arbeitsfläche eingefügt und mit dem normalen Start gebrannt. Danach misst der Bediener Achsen und Kreis nach und trägt
// die Werte hier ein; der Dialog berechnet die Abweichung und die Korrekturfaktoren und legt sie auf Knopfdruck im
// Maschinenprofil ab. Der Dialog ist nicht modal: Die Arbeitsfläche dahinter und der Not-Aus bleiben bedienbar.
#pragma once

#include <QDialog>
#include <QTimer>
#include <optional>

#include "core/calibration.h"
#include "core/machinesetup.h"

class QLabel;
class QPushButton;

namespace ld::app {

class LengthSpinBox;
class TextPreview;

class CalibrationDialog : public QDialog {
    Q_OBJECT

public:
    CalibrationDialog(const ld::core::MachineSetup& machine, const std::optional<ld::core::CalibrationSpec>& initial,
                      QWidget* parent = nullptr);

    // Anderes Maschinenprofil (oder neue Korrekturwerte): Beschriftungen und aktuelle Faktoren neu zeigen.
    void setMachine(const ld::core::MachineSetup& machine);
    ld::core::CalibrationSpec spec() const;

signals:
    // Die Grafik soll in die Arbeitsfläche eingefügt werden (ersetzt das Bild).
    void insertRequested(const ld::core::CalibrationSpec& spec);
    // Neue Faktoren für die Maschinenachsen X und Y ins Profil (ein leerer Wert bleibt unverändert).
    void saveFactorsRequested(std::optional<double> machineX, std::optional<double> machineY);

protected:
    void done(int result) override;

private:
    void buildUi();
    void scheduleUpdate();
    void updatePreview();
    void updateResults();
    void updateLabels();

    ld::core::MachineSetup machine_;
    ld::core::CalibrationShape shape_;

    LengthSpinBox* lengthX_ = nullptr;
    LengthSpinBox* lengthY_ = nullptr;
    LengthSpinBox* diameter_ = nullptr;
    LengthSpinBox* measuredAxisX_ = nullptr;
    LengthSpinBox* measuredCircleX_ = nullptr;
    LengthSpinBox* measuredAxisY_ = nullptr;
    LengthSpinBox* measuredCircleY_ = nullptr;
    QLabel* nominalAxisX_ = nullptr;
    QLabel* nominalCircleX_ = nullptr;
    QLabel* nominalAxisY_ = nullptr;
    QLabel* nominalCircleY_ = nullptr;
    QLabel* deviationAxisX_ = nullptr;
    QLabel* deviationCircleX_ = nullptr;
    QLabel* deviationAxisY_ = nullptr;
    QLabel* deviationCircleY_ = nullptr;
    QLabel* axisXName_ = nullptr;
    QLabel* circleXName_ = nullptr;
    QLabel* axisYName_ = nullptr;
    QLabel* circleYName_ = nullptr;
    // Ergebnistabelle: je Zeile (waagerecht, senkrecht) Maschinenachse, Faktor im Profil und neuer Faktor
    QLabel* resultAxisH_ = nullptr;
    QLabel* resultAxisV_ = nullptr;
    QLabel* resultCurrentH_ = nullptr;
    QLabel* resultCurrentV_ = nullptr;
    QLabel* resultNewH_ = nullptr;
    QLabel* resultNewV_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    TextPreview* preview_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QTimer updateTimer_;
    // Neue Faktoren (waagerecht / senkrecht aus Sicht des Bedieners), leer = nicht gemessen.
    std::optional<double> factorHorizontal_;
    std::optional<double> factorVertical_;
};

}  // namespace ld::app
