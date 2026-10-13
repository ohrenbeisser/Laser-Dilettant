#include "device/FocusLaser.h"

#include "core/Units.h"
#include "device/GrblDriver.h"

FocusLaser::FocusLaser(GrblDriver* driver, QObject* parent)
    : QObject(parent)
    , m_driver(driver)
{
    m_autoOff.setSingleShot(true);
    connect(&m_autoOff, &QTimer::timeout, this, [this] {
        if (m_on) {
            turnOff();
            emit message(tr("Focus laser switched off automatically after %1 s.").arg(m_autoOffSeconds));
        }
    });

    // GRBL switches the laser off on every reset (stop, emergency stop).
    connect(m_driver, &GrblDriver::resetDetected, this, [this] {
        setOn(false);
        restoreLaserMode();
    });
    connect(m_driver, &GrblDriver::emergencyStopped, this, [this] { setOn(false); });
    connect(m_driver, &GrblDriver::alarmRaised, this, [this] { setOn(false); });
    connect(m_driver, &GrblDriver::aboutToDisconnect, this, [this] {
        if (m_on)
            turnOff();
        else
            restoreLaserMode();
    });
    connect(m_driver, &GrblDriver::disconnected, this, [this] { setOn(false); });
    connect(m_driver, &GrblDriver::connectionLost, this, [this] { setOn(false); });
    connect(m_driver, &GrblDriver::connected, this, [this] {
        if (m_pendingRestore >= 0 && m_driver->settings().has(32)
            && qRound(m_driver->settings().number(32)) == m_pendingRestore) {
            setPending(-1);
        } else if (m_pendingRestore >= 0) {
            emit message(tr("Laser mode was switched off by the focus laser and is now restored ($32=%1).")
                             .arg(m_pendingRestore));
            restoreLaserMode();
        }
    });
}

void FocusLaser::setAutoOffSeconds(int seconds)
{
    m_autoOffSeconds = qMax(1, seconds);
}

int FocusLaser::secondsLeft() const
{
    return m_autoOff.isActive() ? (m_autoOff.remainingTime() + 999) / 1000 : 0;
}

void FocusLaser::setOn(bool on)
{
    if (!on)
        m_autoOff.stop();
    if (m_on == on)
        return;
    m_on = on;
    emit stateChanged(on);
}

void FocusLaser::setPending(int value)
{
    if (m_pendingRestore == value)
        return;
    m_pendingRestore = value;
    emit pendingRestoreChanged(value);
}

bool FocusLaser::turnOn(double percent, double maxS, QString* error)
{
    if (!m_driver->isConnected()) {
        if (error)
            *error = tr("Not connected.");
        return false;
    }
    if (m_driver->isJobRunning()) {
        if (error)
            *error = tr("Not possible while a job is running.");
        return false;
    }
    const MachineState st = m_driver->status().state;
    if (st != MachineState::Idle) {
        if (error)
            *error = tr("The machine must be idle (current state: %1).").arg(machineStateName(st));
        return false;
    }

    if (m_disableLaserMode && m_driver->settings().number(32) > 0.5) {
        setPending(1);
        m_driver->sendCommand(QStringLiteral("$32=0"));
    }
    const double s = Units::percentToS(qBound(0.0, percent, 100.0), maxS);
    m_driver->sendCommand(QStringLiteral("M3 S") + Units::gcodeNumber(s, 1));
    m_driver->sendCommand(QStringLiteral("G1 F100"));
    setOn(true);
    m_autoOff.start(m_autoOffSeconds * 1000);
    return true;
}

void FocusLaser::turnOff()
{
    setOn(false);
    m_driver->sendCommand(QStringLiteral("M5"));
    restoreLaserMode();
}

void FocusLaser::restoreLaserMode()
{
    if (m_pendingRestore < 0 || !m_driver->transport())
        return;
    const int value = m_pendingRestore;
    m_driver->sendCommand(QStringLiteral("$32=%1").arg(value), [this, value](bool ok, const QString&) {
        if (ok && m_pendingRestore == value)
            setPending(-1);
    });
}
