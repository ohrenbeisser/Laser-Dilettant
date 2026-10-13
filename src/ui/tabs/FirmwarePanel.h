#pragma once

#include "device/GrblTypes.h"

#include <QWidget>

class AppSettings;
class GrblDriver;
class ProfileStore;
class QLabel;
class QLineEdit;
class QPushButton;

// Firmware information, backup and flashing (section 9.4).
class FirmwarePanel : public QWidget
{
    Q_OBJECT
public:
    FirmwarePanel(GrblDriver* driver, AppSettings* settings, ProfileStore* profiles, QWidget* parent = nullptr);

    // Provided by the main window: profile id and port currently selected.
    void setContext(const QString& profileId, const ConnectionParams& port);

signals:
    void wizardStarted();
    // The wizard finished; the profile may have changed (new baud rate).
    void wizardFinished(const QString& profileId);

private:
    void refresh();
    void backupFirmware();
    void runWizard(bool restore);
    bool checkPrerequisites();

    GrblDriver* m_driver;
    AppSettings* m_settings;
    ProfileStore* m_profiles;
    QString m_profileId;
    ConnectionParams m_port;

    QLabel* m_version;
    QLabel* m_buildInfo;
    QLineEdit* m_avrdudePath;
    QLabel* m_avrdudeStatus;
    QPushButton* m_backup;
    QPushButton* m_flash;
    QPushButton* m_restore;
};
