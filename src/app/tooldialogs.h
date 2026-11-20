// ToolDialogs: Die beiden nicht-modalen Werkzeug-Dialoge Materialtest und Kalibrierung (zerlegt aus dem MainWindow,
// siehe planung.md 3.33). Er öffnet sie (immer nur einer je Art), verbindet sie mit JobController (Lauf am Laser),
// ContentController (Messgrafik einfügen) und Maschinenprofil (Korrekturfaktoren speichern) und gibt ihnen Zustand
// und Profil weiter. Das MainWindow reagiert nur auf seine Signale.
#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <optional>

#include "core/engraving.h"
#include "core/machinesetup.h"
#include "core/materialtest.h"

class QWidget;

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class CalibrationDialog;
class ContentController;
class JobController;
class MachineProfiles;
class MaterialTestDialog;

class ToolDialogs : public QObject {
    Q_OBJECT

public:
    // frameValues: Rahmen-Speed (mm/min) und -Leistung (%) aus dem rechten Panel, für den Rahmen um den Materialtest.
    // pixelSize: Auflösung aus dem rechten Panel (Vorgabe des Materialtests, wenn es kein Bild gibt).
    ToolDialogs(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine, MachineProfiles* profiles,
                JobController* jobs, ContentController* content, std::function<double()> pixelSize,
                std::function<std::pair<double, double>()> frameValues, QWidget* dialogParent, QObject* parent = nullptr);

    void openMaterialTest();
    void openCalibration();
    // Zustand des Lasers für den Materialtest (Start und Rahmen gehen nur, wenn verbunden und bereit).
    void setRunState(bool connected, bool idle);
    // Das Maschinenprofil hat sich geändert: Der Materialtest schließt sich, die Kalibrierung zeigt das neue Profil.
    void onMachineChanged();

    // Zum Prüfen in Tests: die offenen Dialoge (null, wenn keiner offen ist).
    MaterialTestDialog* materialTestDialog() const { return materialTest_; }
    CalibrationDialog* calibrationDialog() const { return calibration_; }

signals:
    void message(const QString& text, int milliseconds);   // Statusleiste
    // Der Materialtest will das gewählte Feld als Material ablegen (Werte, Notiz).
    void materialRequested(const ld::core::Operation& operation, const QString& note);
    // Die Korrekturfaktoren wurden ins Profil geschrieben; das MainWindow wendet das Profil an.
    void machineSaved(const ld::core::MachineSetup& machine);

private:
    void onSaveGcode(const ld::core::MaterialTestSpec& spec);
    void onSaveMaterial(double speedMmPerMin, double powerPercent, ld::core::EngraveMode mode);
    void onSaveFactors(std::optional<double> machineX, std::optional<double> machineY);

    ld::core::ImageDocument* document_;
    const ld::core::MachineSetup& machine_;
    MachineProfiles* profiles_;
    JobController* jobs_;
    ContentController* content_;
    std::function<double()> pixelSize_;
    std::function<std::pair<double, double>()> frameValues_;
    QWidget* dialogParent_;
    QPointer<MaterialTestDialog> materialTest_;
    QPointer<CalibrationDialog> calibration_;
};

}  // namespace ld::app
