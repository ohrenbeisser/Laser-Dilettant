#pragma once

#include "device/GrblSettings.h"
#include "device/GrblStreamer.h"
#include "device/IDeviceDriver.h"
#include "device/ITransport.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <functional>



// Driver for GRBL 1.1 controllers (including manufacturer variants such as
// "Grbl 1.1z" and grblHAL in compatibility mode).
class GrblDriver : public IDeviceDriver
{
    Q_OBJECT
public:
    // Real-time commands (sent without line end, not buffered by GRBL).
    enum Realtime : char {
        StatusQuery = '?',
        FeedHold = '!',
        CycleStart = '~',
        SoftReset = 0x18,
        JogCancel = static_cast<char>(0x85),
        FeedOv100 = static_cast<char>(0x90),
        FeedOvPlus10 = static_cast<char>(0x91),
        FeedOvMinus10 = static_cast<char>(0x92),
        FeedOvPlus1 = static_cast<char>(0x93),
        FeedOvMinus1 = static_cast<char>(0x94),
        SpindleOv100 = static_cast<char>(0x99),
        SpindleOvPlus10 = static_cast<char>(0x9A),
        SpindleOvMinus10 = static_cast<char>(0x9B),
        SpindleOvPlus1 = static_cast<char>(0x9C),
        SpindleOvMinus1 = static_cast<char>(0x9D),
    };

    using TransportFactory = std::function<ITransport*(const ConnectionParams&)>;

    explicit GrblDriver(QObject* parent = nullptr);
    ~GrblDriver() override;

    // Lets tests (and the "Simulator" port) provide the transport.
    void setTransportFactory(TransportFactory factory) { m_factory = std::move(factory); }
    ITransport* transport() const { return m_transport; }

    // --- IDeviceDriver ------------------------------------------------------
    void connectDevice(const ConnectionParams& params) override;
    void disconnectDevice() override;
    bool isConnected() const override { return m_phase == Phase::Ready; }

    void startJob(const GcodeProgram& program, int passes) override;
    void pause() override;
    void resume() override;
    void stop() override;
    void emergencyStop() override;
    bool isJobRunning() const override { return m_jobRunning; }

    void jog(double dx, double dy, double feedMmMin) override;
    void jogCancel() override;
    void sendCommand(const QString& line, ReplyHandler handler = {}) override;

    MachineStatus status() const override { return m_status; }

    // --- GRBL specific ------------------------------------------------------
    bool isConnecting() const;
    void sendRealtime(char c);
    void requestSettings(ReplyHandler done = {});
    // Writes "$N=value" one after another (GRBL writes each to its EEPROM).
    // done() receives one message per rejected setting.
    void writeSettings(const QList<QPair<int, QString>>& values, std::function<void(const QStringList& errors)> done);
    void softReset();
    void unlock() { sendCommand(QStringLiteral("$X")); }
    void home() { sendCommand(QStringLiteral("$H")); }
    void setFeedOverride(int percent);
    void setSpindleOverride(int percent);

    void setPollInterval(int ms);
    int pollInterval() const { return m_pollTimer.interval(); }
    // Time without status report after which the connection counts as lost.
    void setWatchdogTimeout(int ms) { m_watchdogMs = ms; }
    void setBannerTimeouts(int firstMs, int afterResetMs);

    const GrblSettings& settings() const { return m_settings; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    QString bannerLine() const { return m_bannerLine; }
    QStringList buildInfo() const { return m_buildInfo; }
    QString parserState() const { return m_parserState; }
    const ConnectionParams& connectionParams() const { return m_params; }
    const GrblStreamer& streamer() const { return m_streamer; }
    qint64 jobElapsedMs() const { return m_jobRunning ? m_jobClock.elapsed() : m_jobElapsedMs; }

signals:
    void connectFailed(const QString& message);
    void connectionLost(const QString& message);
    // Emitted before the port is closed so others can switch things off.
    void aboutToDisconnect();
    void settingsReceived();
    void buildInfoReceived();
    // GRBL announced a reset (banner) while connected.
    void resetDetected();
    void emergencyStopped();
    // Every byte sequence written to the device (for the communication log).
    void bytesWritten(const QByteArray& data);
    // An error occurred during a job; the job is held until resume() or stop().
    void jobErrorPaused(qint64 lineNumber, const QString& line, int errorCode);

private:
    enum class Phase { Closed, WaitBanner, WaitBannerAfterReset, Initializing, Ready, Disconnecting };

    void onData(const QByteArray& data);
    void onTransportError(const QString& message, bool fatal);
    void handleLine(const QString& line);
    void handleReply(bool ok, const QString& line);
    void handleBanner(const QString& line, const QString& version);
    void onBannerTimeout();
    void onPoll();
    void pump();
    void write(const QByteArray& data);
    void beginInitialization();
    void finishJob(bool completed);
    void closeTransport();
    void setPhase(Phase p);
    void sendOverrideSteps(int current, int target, char plus10, char minus10, char plus1, char minus1);

    TransportFactory m_factory;
    QPointer<ITransport> m_transport;
    ConnectionParams m_params;
    Phase m_phase = Phase::Closed;

    QByteArray m_rxBuffer;
    GrblStreamer m_streamer;
    GrblSettings m_settings;
    GrblSettings m_settingsBuffer;
    MachineStatus m_status;
    QString m_firmwareVersion;
    QString m_bannerLine;
    QStringList m_buildInfo;
    QStringList m_buildInfoBuffer;
    QString m_parserState;

    QTimer m_pollTimer;
    QTimer m_bannerTimer;
    QTimer m_stopTimer;
    QTimer m_disconnectTimer;
    QElapsedTimer m_sinceStatus;
    int m_watchdogMs = 3000;
    int m_bannerTimeoutMs = 5000;
    int m_bannerRetryMs = 3000;

    bool m_jobRunning = false;
    bool m_stopping = false;
    bool m_jobErrorHeld = false;
    bool m_m5AfterReset = false;
    QElapsedTimer m_jobClock;
    qint64 m_jobElapsedMs = 0;

    int m_expectedOvFeed = 100;
    int m_expectedOvSpindle = 100;
    QElapsedTimer m_sinceOverride;
};
