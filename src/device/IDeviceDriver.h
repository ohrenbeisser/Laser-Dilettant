#pragma once

#include "device/GrblTypes.h"
#include "gcode/GcodeProgram.h"

#include <QObject>
#include <QString>

#include <functional>

// Interface for laser controllers. Version 1 has only the GRBL driver, but
// other controllers (Marlin, Smoothieware, M2 Nano) can be added later.
class IDeviceDriver : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDeviceDriver() override = default;

    // Called when the device answered a command: ok == false means error,
    // response holds "ok" or "error:N".
    using ReplyHandler = std::function<void(bool ok, const QString& response)>;

    // Connection
    virtual void connectDevice(const ConnectionParams& params) = 0;
    virtual void disconnectDevice() = 0;
    virtual bool isConnected() const = 0;

    // Job
    virtual void startJob(const GcodeProgram& program, int passes) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;          // controlled stop, keeps position
    virtual void emergencyStop() = 0; // immediate
    virtual bool isJobRunning() const = 0;

    // Manual control
    virtual void jog(double dx, double dy, double feedMmMin) = 0;
    virtual void jogCancel() = 0;
    virtual void sendCommand(const QString& line, ReplyHandler handler = {}) = 0;

    virtual MachineStatus status() const = 0;

signals:
    void stateChanged(MachineState state);
    void statusUpdated(const MachineStatus& status);
    void positionChanged(double x, double y);
    void progressChanged(qint64 acknowledged, qint64 total);
    void messageReceived(const QString& message);
    void lineSent(const QString& line);
    void lineReceived(const QString& line, bool isStatusReport);
    void errorOccurred(const QString& message);
    void alarmRaised(int code, const QString& text);
    void connected();
    void disconnected();
    void jobFinished(bool completed);
};
