#pragma once

#include "core/DeviceProfile.h"
#include "device/GrblTypes.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

// Top bar: port, baud rate, connect button, state, profile, emergency stop.
class ConnectionBar : public QWidget
{
    Q_OBJECT
public:
    static constexpr const char* kSimulatorPort = "<simulator>";

    explicit ConnectionBar(QWidget* parent = nullptr);

    void refreshPorts();
    void setProfiles(const QList<DeviceProfile>& profiles, const QString& currentId);
    QString currentProfileId() const;
    void setBaudRate(int baud);
    void selectPort(const QString& port);

    ConnectionParams params() const;
    void setMachineState(MachineState s);
    // While connected (or connecting) port, baud and profile are locked.
    void setConnectionActive(bool active);
    void setProfileChangeAllowed(bool allowed);

signals:
    void connectClicked();
    void disconnectClicked();
    void emergencyStopClicked();
    void profileSelected(const QString& id);

private:
    QComboBox* m_port;
    QToolButton* m_refresh;
    QComboBox* m_baud;
    QPushButton* m_connect;
    QLabel* m_state;
    QComboBox* m_profile;
    QPushButton* m_estop;
    bool m_active = false;
};
