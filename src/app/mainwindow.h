// Hauptfenster von Laser-Dilettant (Meilenstein M0: Gerüst wie im Mockup, noch ohne Funktion).
//
// Aufbau:
//   oben     Menüleiste, rechts daneben Hell/Dunkel-Umschalter und NOT-AUS
//   links    einblendbare Seitenleiste (LeftPanel)
//   Mitte    Arbeitsfläche (Platzhalter bis M1)
//   rechts   Panel (RightPanel) mit festem Fuß für Start/Pause/Rahmen
//   unten    einblendbare Konsole, darunter die Statusleiste
#pragma once

#include <memory>

#include "core/svgmodel.h"

#include <QElapsedTimer>
#include <QMap>
#include <QMainWindow>
#include <QPointer>
#include <optional>
#include <QTemporaryDir>

#include "grbl/grblconnection.h"
#include "grbl/weaklight.h"

#include "jobcontroller.h"

#include "core/calibration.h"
#include "core/machinesetup.h"
#include "core/materialtest.h"
#include "core/ncloader.h"
#include <QString>

class QImage;

class QAction;
class QDockWidget;
class QPushButton;
class QCloseEvent;
class QLabel;
class QMenu;
class QProgressBar;
class QProgressDialog;

namespace ld::core {
class ImageDocument;
class GcodeRunner;
class PathPreview;
class Simulation;
struct GcodeResult;
}

namespace ld::app {

class CanvasView;
class ConnectionController;
class ProtocolController;
class ProtocolDialog;
class NotificationController;
class RemoteController;
class ContentController;
class ToolDialogs;
class ConsoleWidget;
class JogController;
class LeftPanel;
class MachineProfiles;
class ProjectController;
class MaterialTestDialog;
class CalibrationDialog;
class PreviewBar;
class RightPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Nach dem Start: Sicherung nach einem Absturz anbieten und ein Projekt aus den Programmargumenten öffnen.
    void handleStartup(const QStringList& arguments);

protected:
    // Beim Schließen Fenstergröße und Lage der Leisten merken.
    void closeEvent(QCloseEvent* event) override;
    // Tastatur: Pfeiltasten = Jog, Leertaste = Pause/Fortsetzen (nur, wenn kein Eingabefeld den Fokus hat).
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void toggleColorScheme();
    void onEmergencyStop();
    // Verbindung zum Laser (M4)
    void setupConnection();
    void updateConnectionUi();
    void updateGrblLabel();
    void refreshPortList();
    // Steuerung und Lauf am Laser (M4 Teil 2)
    void startLaserJob();
    void onTestSizeClicked();
    // Materialtest (siehe planung.md 3.20)
    void openMaterialTest();
    void openProtocol();
    // NC-Auftrag (siehe planung.md 3.21)
    void loadNcFile(const QString& path = QString(), bool silent = false);
    void onNcChecked(const ld::core::NcAnalysis& result, const QString& src, const QString& dst, bool silent);
    bool confirmNcIssues(const ld::core::NcAnalysis& result);
    void clearWorkArea();
    void leaveNcMode();
    bool jobAvailable() const;   // es gibt einen Auftrag zum Zeigen, Simulieren und Brennen (Bild oder NC-Datei)
    void updateMotionEnabled();
    void onJobStarted(JobController::Kind kind);
    void onJobFinished(JobController::Kind kind);
    // M4 Teil 3: GRBL-Parameter, Selbsttest, Baudrate testen, Auto-Verbindung
    void openGrblSettings();
    // M8 Teil 1: Schwachlicht und Live-Regler
    void updateWeakLightUi();
    void applyLaserSettings();
    void beginLive();
    void endLive();
    void showAbout();
    void updateSizeWarning();
    void saveGcode();
    void openMachineProfiles();
    void rebuildRecentMenu();
    void openSettings();
    void applyMachine(const ld::core::MachineSetup& machine);
    void applyDesign();
    void switchProfile(const QString& name);
    void updateDurationLabel();
    void onSaveFinished(const ld::core::GcodeResult& result, const QString& path);

    // Vorschau und Simulation (M5)
    void onJobChanged();
    void onPreviewStateChanged();
    void onStartClicked();
    void onPauseClicked();
    void onSimulationClicked();
    void onSimulationPauseClicked();
    void beginSimulation();
    void stopSimulation(const QString& reason);
    void onSimulationAdvanced(size_t first, size_t last, const QPointF& head, double elapsedSeconds);
    void onSimulationFinished();
    void showPreviewInfo();
    void updateRunButtons();
    bool checkWorkArea();   // false (mit Meldung), wenn der Auftrag zu groß ist
    void updateWindowTitle();

private:
    void createMenus();
    void createCornerWidget();
    void createCentralArea();
    void createDocks();
    void createStatusBar();
    void updateThemeButtonText();


    ld::core::ImageDocument* document_ = nullptr;
    CanvasView* canvas_ = nullptr;
    bool darkActive_ = false;   // das dunkle Design ist gerade aktiv
    PreviewBar* previewBar_ = nullptr;
    RightPanel* rightPanel_ = nullptr;
    QLabel* sizeWarning_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    MachineProfiles* profiles_ = nullptr;
    ProjectController* projectController_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    LeftPanel* leftPanel_ = nullptr;
    ld::core::MachineSetup machine_;
    ld::core::GcodeRunner* saver_ = nullptr;       // schreibt die G-Code-Datei im Hintergrund
    QProgressDialog* saveProgress_ = nullptr;

    // Vorschau und Simulation
    ld::core::PathPreview* pathPreview_ = nullptr;
    ld::core::Simulation* simulation_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* grblLabel_ = nullptr;
    QLabel* mouseLabel_ = nullptr;
    bool simStartPending_ = false;  // Simulation wurde gedrückt, die Wege werden noch erzeugt
    ProtocolController* protocol_ = nullptr;   // Auftrags- und Systemprotokoll (planung.md 3.38)
    QPointer<ProtocolDialog> protocolDialog_;
    NotificationController* notifier_ = nullptr;
    RemoteController* remote_ = nullptr;   // Fernansicht (planung.md 3.41)   // Benachrichtigung über ntfy (planung.md 3.40)
    ContentController* content_ = nullptr;  // Bild, Text, Messgrafik laden bzw. einfügen (planung.md 3.33)
    ToolDialogs* tools_ = nullptr;          // Materialtest- und Kalibrierdialog
    JobController* jobs_ = nullptr; // Läufe am Laser (Bild, Rahmen, Materialtest), siehe planung.md 3.33
    QPushButton* themeButton_ = nullptr;
    QPushButton* emergencyButton_ = nullptr;
    QDockWidget* leftDock_ = nullptr;
    QDockWidget* rightDock_ = nullptr;
    QDockWidget* consoleDock_ = nullptr;
    ConsoleWidget* console_ = nullptr;
    ConnectionController* connection_ = nullptr;   // Anschluss, Selbsttest, Alarme, Nullpunkt (planung.md 3.33)
    ld::grbl::GrblConnection* grbl_ = nullptr;     // gehört dem ConnectionController
    QLabel* connLed_ = nullptr;
    QLabel* connText_ = nullptr;
    QLabel* posLabel_ = nullptr;
    JogController* jog_ = nullptr;
    bool ncMode_ = false;          // NC-Auftrag (geladene Datei) statt Bild
    QString ncSourcePath_;
    ld::core::NcLoader* ncLoader_ = nullptr;
    QProgressDialog* ncProgress_ = nullptr;
    int ncCounter_ = 0;
    bool ncFitPending_ = false;    // nach dem Laden die Ansicht einpassen
    bool ncSilent_ = false;        // beim erneuten Laden (zum Beispiel nach einem Profilwechsel) keine Rückfragen
    QAction* closeNcAction_ = nullptr;
    bool calibrationModeWarned_ = false;   // Hinweis "Messgrafik braucht Cutting" nur einmal je Moduswechsel
    QTemporaryDir ncDir_;          // bereinigte Kopien geladener NC-Dateien
    int heldKey_ = 0;
    QAction* grblParamsAction_ = nullptr;
    QAction* firmwareAction_ = nullptr;
    QAction* unlockAction_ = nullptr;
    ld::grbl::WeakLight* weak_ = nullptr;
    QLabel* laserOnLabel_ = nullptr;
};

}  // namespace ld::app
