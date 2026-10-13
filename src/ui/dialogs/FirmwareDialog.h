#pragma once

#include "device/GrblSettings.h"
#include "device/GrblTypes.h"

#include <QWizard>

class AppSettings;
class AvrdudeRunner;
class GrblDriver;
class ProfileStore;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;

// Guided firmware change (section 9.4):
//  1. warning and confirmation
//  2. back up the $$ settings (mandatory)
//  3. back up the firmware (strongly recommended)
//  4. flash, with live avrdude output
//  5. reconnect with the new baud rate, adjust the profile
//  6. restore the backed-up settings, check $32
class FirmwareDialog : public QWizard
{
    Q_OBJECT
public:
    enum class Mode {
        FlashGrbl,       // bundled GRBL 1.1h or a custom file
        RestoreBackup,   // write a previously saved firmware backup
    };

    FirmwareDialog(Mode mode, GrblDriver* driver, AppSettings* settings, ProfileStore* profiles,
                   const QString& profileId, const ConnectionParams& port, QWidget* parent = nullptr);
    ~FirmwareDialog() override;

    // The profile that should be active afterwards (may be a new copy).
    QString resultProfileId() const { return m_profileId; }

    // Boot loader baud rates of the Arduino Nano (old: 57600, new: 115200).
    static QComboBox* createBootBaudCombo(QWidget* parent);
    AvrdudeRunner* avrdude() const { return m_avrdude; }

protected:
    void reject() override;

private:
    friend class FwIntroPage;
    friend class FwSettingsPage;
    friend class FwBackupPage;
    friend class FwFlashPage;
    friend class FwReconnectPage;
    friend class FwRestorePage;

    QString portName() const { return m_port.portName; }
    int bootBaud() const { return m_bootBaudValue; }
    void setBootBaud(int baud) { m_bootBaudValue = baud; }
    void closeConnection();
    QString bundledHexPath();

    Mode m_mode;
    GrblDriver* m_driver;
    AppSettings* m_settings;
    ProfileStore* m_profiles;
    QString m_profileId;
    ConnectionParams m_port;
    AvrdudeRunner* m_avrdude;
    int m_bootBaudValue = 57600;

    // Results of the steps
    QString m_settingsFile;
    GrblSettings m_savedSettings;
    QString m_firmwareBackupFile;
    bool m_firmwareBackupSkipped = false;
    bool m_flashed = false;
    QString m_tempHex;
};
