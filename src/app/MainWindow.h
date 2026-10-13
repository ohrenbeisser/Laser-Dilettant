#pragma once

#include "core/DeviceProfile.h"
#include "device/GrblTypes.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>

class AppSettings;
class CommLog;
class ConnectionBar;
class ControlTab;
class FocusLaser;
class GrblDriver;
class JobBar;
class KeyboardFilter;
class ProfileStore;
class QMessageBox;
class QSplitter;
class QTabWidget;
class QToolButton;
class SettingsTab;
class StatusLine;
class WorkArea;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppSettings* settings, QWidget* parent = nullptr);
    ~MainWindow() override;

    GrblDriver* driver() const { return m_driver; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void connectDriver();
    void applySettings();
    void restoreWindowState();
    void saveWindowState();

    void activateProfile(const QString& id);
    void refreshProfileList();

    void onConnectClicked();
    void onDisconnectClicked();
    void onConnected();
    void onStateChanged(MachineState state);
    void updateControls();
    void emergencyStop();
    void runPostConnectChecks();
    void showAlarmDialog(int code, const QString& text);
    void checkLaserMode();
    void checkProfileAgainstDevice();
    void showInfo(const QString& title, const QString& text, bool warning);
    void setTabsCollapsed(bool collapsed);

    AppSettings* m_settings;
    ProfileStore* m_profiles;
    GrblDriver* m_driver;
    FocusLaser* m_focus;
    CommLog* m_commLog;
    KeyboardFilter* m_keyboard;
    DeviceProfile m_profile;

    ConnectionBar* m_connectionBar = nullptr;
    WorkArea* m_workArea = nullptr;
    JobBar* m_jobBar = nullptr;
    StatusLine* m_statusLine = nullptr;
    QSplitter* m_splitter = nullptr;
    QTabWidget* m_tabs = nullptr;
    ControlTab* m_controlTab = nullptr;
    SettingsTab* m_settingsTab = nullptr;
    QToolButton* m_collapseTabs = nullptr;

    QPointer<QMessageBox> m_infoBox;
    QElapsedTimer m_lastEmergencyStop;
    QList<int> m_splitterSizes;
    bool m_wizardActive = false;
    bool m_closing = false;
};
