#include "device/GrblDriver.h"

#include "core/Units.h"
#include "device/GrblCodes.h"
#include "device/GrblStatusParser.h"
#include "device/ITransport.h"
#include "device/SerialTransport.h"
#include "device/SimulatedGrbl.h"

#include <QRegularExpression>

#include <memory>

namespace {
const QRegularExpression& bannerRegex()
{
    static const QRegularExpression re(QStringLiteral("^Grbl(?:HAL)?\\s+(\\S+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}
} // namespace

GrblDriver::GrblDriver(QObject* parent)
    : IDeviceDriver(parent)
{
    qRegisterMetaType<MachineState>();
    qRegisterMetaType<MachineStatus>();

    m_pollTimer.setInterval(200);
    connect(&m_pollTimer, &QTimer::timeout, this, &GrblDriver::onPoll);
    m_bannerTimer.setSingleShot(true);
    connect(&m_bannerTimer, &QTimer::timeout, this, &GrblDriver::onBannerTimeout);
    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        // The machine did not report standstill in time: reset anyway.
        if (m_stopping)
            softReset();
    });
    m_disconnectTimer.setSingleShot(true);
    connect(&m_disconnectTimer, &QTimer::timeout, this, [this] {
        if (m_phase == Phase::Disconnecting) {
            closeTransport();
            emit disconnected();
        }
    });

    m_factory = [](const ConnectionParams& p) -> ITransport* {
        if (p.simulator) {
            auto* sim = new SimulatedGrbl;
            sim->setBaudRate(p.baudRate);
            return sim;
        }
        return new SerialTransport(p.portName, p.baudRate);
    };
}

GrblDriver::~GrblDriver()
{
    if (m_transport) {
        m_transport->close();
        delete m_transport;
    }
}

bool GrblDriver::isConnecting() const
{
    return m_phase == Phase::WaitBanner || m_phase == Phase::WaitBannerAfterReset || m_phase == Phase::Initializing;
}

void GrblDriver::setPhase(Phase p)
{
    m_phase = p;
}

void GrblDriver::setPollInterval(int ms)
{
    m_pollTimer.setInterval(qBound(50, ms, 5000));
}

void GrblDriver::setBannerTimeouts(int firstMs, int afterResetMs)
{
    m_bannerTimeoutMs = firstMs;
    m_bannerRetryMs = afterResetMs;
}

// --- connection ---------------------------------------------------------------

void GrblDriver::connectDevice(const ConnectionParams& params)
{
    if (m_phase != Phase::Closed)
        closeTransport();

    m_params = params;
    m_rxBuffer.clear();
    m_streamer.clear();
    m_settings.clear();
    m_settingsBuffer.clear();
    m_buildInfo.clear();
    m_firmwareVersion.clear();
    m_bannerLine.clear();
    m_parserState.clear();
    m_status = MachineStatus();
    m_status.state = MachineState::Connecting;
    m_expectedOvFeed = m_expectedOvSpindle = 100;

    ITransport* t = m_factory(params);
    if (!t) {
        emit connectFailed(tr("Could not create the connection."));
        return;
    }
    t->setParent(this);
    m_transport = t;
    connect(t, &ITransport::dataReceived, this, &GrblDriver::onData);
    connect(t, &ITransport::errorOccurred, this, &GrblDriver::onTransportError);

    QString error;
    if (!t->open(&error)) {
        delete t;
        m_transport = nullptr;
        m_status.state = MachineState::Disconnected;
        emit stateChanged(m_status.state);
        emit connectFailed(error.isEmpty() ? tr("The port could not be opened.") : error);
        return;
    }
    emit stateChanged(m_status.state);
    // Opening the port resets the Arduino (DTR). Wait for the banner.
    setPhase(Phase::WaitBanner);
    m_bannerTimer.start(m_bannerTimeoutMs);
}

void GrblDriver::onBannerTimeout()
{
    if (m_phase == Phase::WaitBanner) {
        // No banner: maybe the board did not reset. Ask GRBL for one.
        setPhase(Phase::WaitBannerAfterReset);
        write(QByteArray(1, SoftReset));
        m_bannerTimer.start(m_bannerRetryMs);
        return;
    }
    if (m_phase == Phase::WaitBannerAfterReset) {
        closeTransport();
        emit connectFailed(tr("No GRBL device found. Check the baud rate."));
        return;
    }
    if (m_phase == Phase::Initializing) {
        closeTransport();
        emit connectFailed(tr("The device does not answer. Check the baud rate."));
    }
}

void GrblDriver::beginInitialization()
{
    setPhase(Phase::Initializing);
    m_bannerTimer.start(10000);
    m_buildInfoBuffer.clear();
    sendCommand(QStringLiteral("$I"), [this](bool, const QString&) {
        m_buildInfo = m_buildInfoBuffer;
        m_buildInfoBuffer.clear();
        emit buildInfoReceived();
    });
    requestSettings();
    sendCommand(QStringLiteral("$G"));
    // Safety: laser off.
    sendCommand(QStringLiteral("M5"), [this](bool, const QString&) {
        if (m_phase != Phase::Initializing)
            return;
        m_bannerTimer.stop();
        setPhase(Phase::Ready);
        m_sinceStatus.restart();
        m_pollTimer.start();
        write(QByteArray(1, StatusQuery));
        emit connected();
    });
}

void GrblDriver::disconnectDevice()
{
    if (m_phase == Phase::Closed)
        return;
    if (m_phase != Phase::Ready) {
        closeTransport();
        emit disconnected();
        return;
    }
    emit aboutToDisconnect();
    if (m_jobRunning) {
        emergencyStop();
    } else {
        sendCommand(QStringLiteral("M5"));
    }
    setPhase(Phase::Disconnecting);
    if (m_streamer.isIdle()) {
        closeTransport();
        emit disconnected();
        return;
    }
    m_disconnectTimer.start(1500);
}

void GrblDriver::closeTransport()
{
    m_pollTimer.stop();
    m_bannerTimer.stop();
    m_stopTimer.stop();
    m_disconnectTimer.stop();
    m_stopping = false;
    if (m_jobRunning)
        finishJob(false);
    m_streamer.clear();
    if (m_transport) {
        ITransport* t = m_transport;
        m_transport = nullptr;
        t->disconnect(this);
        t->close();
        t->deleteLater();
    }
    setPhase(Phase::Closed);
    m_status.state = MachineState::Disconnected;
    emit stateChanged(m_status.state);
    emit statusUpdated(m_status);
}

void GrblDriver::onTransportError(const QString& message, bool fatal)
{
    if (!fatal) {
        emit errorOccurred(message);
        return;
    }
    const bool wasConnecting = isConnecting();
    const bool hadJob = m_jobRunning;
    closeTransport();
    if (wasConnecting) {
        emit connectFailed(message);
        return;
    }
    emit connectionLost(hadJob ? tr("Connection lost during the job: %1").arg(message) : message);
    emit disconnected();
}

// --- receiving ------------------------------------------------------------------

void GrblDriver::onData(const QByteArray& data)
{
    m_rxBuffer.append(data);
    qsizetype nl;
    while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
        QByteArray raw = m_rxBuffer.left(nl);
        m_rxBuffer.remove(0, nl + 1);
        const QString line = QString::fromLatin1(raw).trimmed();
        if (!line.isEmpty())
            handleLine(line);
        if (!m_transport)
            return; // closed while handling
    }
    if (m_rxBuffer.size() > 4096)
        m_rxBuffer.clear(); // garbage (wrong baud rate)
}

void GrblDriver::handleLine(const QString& line)
{
    if (line.startsWith(QLatin1Char('<'))) {
        StatusReport report;
        if (!GrblStatusParser::parse(line, &report))
            return;
        m_sinceStatus.restart();
        const MachineState before = m_status.state;
        GrblStatusParser::merge(report, &m_status);
        if (report.ovFeed && (!m_sinceOverride.isValid() || m_sinceOverride.elapsed() > 1000)) {
            m_expectedOvFeed = m_status.ovFeed;
            m_expectedOvSpindle = m_status.ovSpindle;
        }
        emit lineReceived(line, true);
        emit statusUpdated(m_status);
        if (before != m_status.state)
            emit stateChanged(m_status.state);
        emit positionChanged(m_status.wpos.x, m_status.wpos.y);

        if (m_stopping
            && ((m_status.state == MachineState::Hold && m_status.subState == 0) || m_status.state == MachineState::Idle)) {
            softReset();
        }
        if (m_jobRunning && !m_jobErrorHeld && m_streamer.jobComplete() && m_streamer.isIdle()
            && m_status.state == MachineState::Idle) {
            finishJob(true);
        }
        return;
    }

    emit lineReceived(line, false);

    if (line == QLatin1String("ok")) {
        handleReply(true, line);
    } else if (line.startsWith(QLatin1String("error:"))) {
        handleReply(false, line);
    } else if (line.startsWith(QLatin1String("ALARM:"))) {
        const int code = line.mid(6).toInt();
        if (m_jobRunning) {
            m_streamer.abortJob();
            finishJob(false);
        }
        m_status.state = MachineState::Alarm;
        emit stateChanged(m_status.state);
        emit alarmRaised(code, GrblCodes::alarmText(code));
    } else if (const auto m = bannerRegex().match(line); m.hasMatch()) {
        handleBanner(line, m.captured(1));
    } else if (line.startsWith(QLatin1String("[MSG:"))) {
        emit messageReceived(line.mid(5, line.size() - 6));
    } else if (line.startsWith(QLatin1String("[GC:"))) {
        m_parserState = line.mid(4, line.size() - 5);
    } else if (line.startsWith(QLatin1String("[VER:")) || line.startsWith(QLatin1String("[OPT:"))) {
        m_buildInfoBuffer << line;
    } else if (line.startsWith(QLatin1Char('$'))) {
        m_settingsBuffer.parseAndStore(line);
    }
}

void GrblDriver::handleBanner(const QString& line, const QString& version)
{
    m_firmwareVersion = version;
    m_bannerLine = line;
    // GRBL empties its receive buffer on reset: nothing is in flight anymore.
    m_streamer.clear();
    m_stopping = false;
    m_stopTimer.stop();

    if (m_phase == Phase::WaitBanner || m_phase == Phase::WaitBannerAfterReset) {
        m_bannerTimer.stop();
        beginInitialization();
        return;
    }
    if (m_phase == Phase::Initializing) {
        // Reset during initialisation: start over.
        beginInitialization();
        return;
    }
    if (m_jobRunning) {
        finishJob(false);
        emit errorOccurred(tr("The controller was reset. The job was cancelled."));
    }
    emit resetDetected();
    if (m_m5AfterReset) {
        m_m5AfterReset = false;
        sendCommand(QStringLiteral("M5"));
    }
}

void GrblDriver::handleReply(bool ok, const QString& line)
{
    GrblStreamer::Entry e;
    if (!m_streamer.acknowledge(&e))
        return; // nothing in flight (e.g. after a reset)

    const int code = ok ? 0 : line.mid(6).toInt();
    if (e.job) {
        emit progressChanged(m_streamer.jobAcknowledged(), m_streamer.jobTotal());
        if (!ok) {
            m_jobErrorHeld = true;
            m_streamer.setJobHeld(true);
            write(QByteArray(1, FeedHold));
            emit jobErrorPaused(e.jobIndex + 1, e.line, code);
        }
    } else {
        if (ok) {
            int number = 0;
            QString value;
            if (GrblSettings::parseLine(e.line, &number, &value)) {
                m_settings.set(number, value);
                emit settingsReceived();
            }
        } else if (!e.handler) {
            emit errorOccurred(tr("\"%1\": %2 – %3").arg(e.line, line, GrblCodes::errorText(code)));
        }
        if (e.handler)
            e.handler(ok, line);
    }

    pump();

    if (m_phase == Phase::Disconnecting && m_streamer.isIdle()) {
        closeTransport();
        emit disconnected();
    }
}

void GrblDriver::onPoll()
{
    if (!m_transport || (m_phase != Phase::Ready && m_phase != Phase::Disconnecting))
        return;
    if (m_sinceStatus.isValid() && m_sinceStatus.elapsed() > m_watchdogMs) {
        const bool hadJob = m_jobRunning;
        closeTransport();
        emit connectionLost(hadJob ? tr("The device stopped answering during the job (no status for %1 s).")
                                         .arg(m_watchdogMs / 1000.0)
                                   : tr("The device stopped answering (no status for %1 s).").arg(m_watchdogMs / 1000.0));
        emit disconnected();
        return;
    }
    write(QByteArray(1, StatusQuery));
}

// --- sending --------------------------------------------------------------------

void GrblDriver::write(const QByteArray& data)
{
    if (!m_transport || !m_transport->isOpen())
        return;
    m_transport->write(data);
    emit bytesWritten(data);
}

void GrblDriver::pump()
{
    if (!m_transport)
        return;
    QStringList commands;
    const QList<QByteArray> chunks = m_streamer.takeSendable(&commands);
    if (chunks.isEmpty())
        return;
    QByteArray all;
    for (const QByteArray& c : chunks)
        all += c;
    write(all);
    for (const QString& c : commands)
        emit lineSent(c);
}

void GrblDriver::sendCommand(const QString& line, ReplyHandler handler)
{
    const QString l = line.trimmed();
    const bool canSend = m_transport
        && (m_phase == Phase::Initializing || m_phase == Phase::Ready || m_phase == Phase::Disconnecting);
    if (!canSend || l.isEmpty()) {
        if (handler)
            handler(false, tr("not connected"));
        return;
    }
    m_streamer.enqueue(l, std::move(handler));
    pump();
}

void GrblDriver::sendRealtime(char c)
{
    write(QByteArray(1, c));
}

void GrblDriver::requestSettings(ReplyHandler done)
{
    m_settingsBuffer.clear();
    sendCommand(QStringLiteral("$$"), [this, done](bool ok, const QString& r) {
        if (ok && !m_settingsBuffer.isEmpty()) {
            m_settings = m_settingsBuffer;
            emit settingsReceived();
        }
        m_settingsBuffer.clear();
        if (done)
            done(ok, r);
    });
}

void GrblDriver::writeSettings(const QList<QPair<int, QString>>& values,
                               std::function<void(const QStringList& errors)> done)
{
    if (values.isEmpty()) {
        if (done)
            done({});
        return;
    }
    struct State
    {
        int remaining;
        QStringList errors;
    };
    auto state = std::make_shared<State>(State { static_cast<int>(values.size()), {} });
    for (const auto& [number, value] : values) {
        const QString line = QStringLiteral("$%1=%2").arg(number).arg(value);
        sendCommand(line, [state, line, done](bool ok, const QString& reply) {
            if (!ok) {
                const int code = reply.startsWith(QLatin1String("error:")) ? reply.mid(6).toInt() : 0;
                state->errors << (code ? QStringLiteral("%1: %2").arg(line, GrblCodes::errorText(code))
                                       : QStringLiteral("%1: %2").arg(line, reply));
            }
            if (--state->remaining == 0 && done)
                done(state->errors);
        });
    }
}

void GrblDriver::softReset()
{
    if (!m_transport)
        return;
    const bool wasStopping = m_stopping;
    m_stopping = false;
    m_stopTimer.stop();
    write(QByteArray(1, SoftReset));
    m_streamer.clear();
    if (m_jobRunning)
        finishJob(false);
    // Laser off once GRBL is back (the reset already switches it off; M5
    // also clears the modal spindle state). Sent after the banner, because
    // GRBL empties its receive buffer while resetting.
    if (wasStopping)
        m_m5AfterReset = true;
}

void GrblDriver::emergencyStop()
{
    if (!m_transport)
        return;
    write(QByteArray(1, SoftReset));
    m_stopping = false;
    m_stopTimer.stop();
    m_streamer.clear();
    if (m_jobRunning)
        finishJob(false);
    emit emergencyStopped();
}

// --- job ------------------------------------------------------------------------

void GrblDriver::startJob(const GcodeProgram& program, int passes)
{
    if (!isConnected()) {
        emit errorOccurred(tr("Not connected."));
        return;
    }
    if (m_jobRunning || program.isEmpty())
        return;
    m_jobRunning = true;
    m_jobErrorHeld = false;
    m_streamer.setJob(program.lines, passes);
    m_jobClock.start();
    m_jobElapsedMs = 0;
    emit progressChanged(0, m_streamer.jobTotal());
    pump();
}

void GrblDriver::finishJob(bool completed)
{
    if (!m_jobRunning)
        return;
    m_jobRunning = false;
    m_jobErrorHeld = false;
    m_jobElapsedMs = m_jobClock.isValid() ? m_jobClock.elapsed() : 0;
    if (completed)
        m_streamer.clearJob();
    else
        m_streamer.abortJob();
    emit jobFinished(completed);
}

void GrblDriver::pause()
{
    write(QByteArray(1, FeedHold));
}

void GrblDriver::resume()
{
    if (m_jobErrorHeld) {
        m_jobErrorHeld = false;
        m_streamer.setJobHeld(false);
    }
    write(QByteArray(1, CycleStart));
    pump();
}

void GrblDriver::stop()
{
    if (!m_transport || m_phase != Phase::Ready)
        return;
    m_streamer.setJobHeld(true);
    const MachineState s = m_status.state;
    if (s == MachineState::Idle && !m_jobRunning) {
        softReset();
        m_m5AfterReset = true;
        return;
    }
    // Feed hold first so the axes decelerate and keep their position, then
    // reset when the machine reports standstill (Hold:0).
    m_stopping = true;
    write(QByteArray(1, FeedHold));
    m_stopTimer.start(5000);
}

// --- manual control -------------------------------------------------------------

void GrblDriver::jog(double dx, double dy, double feedMmMin)
{
    QString cmd = QStringLiteral("$J=G91 G21");
    if (dx != 0)
        cmd += QStringLiteral(" X") + Units::gcodeNumber(dx);
    if (dy != 0)
        cmd += QStringLiteral(" Y") + Units::gcodeNumber(dy);
    if (dx == 0 && dy == 0)
        return;
    cmd += QStringLiteral(" F") + Units::gcodeNumber(feedMmMin, 0);
    sendCommand(cmd);
}

void GrblDriver::jogCancel()
{
    write(QByteArray(1, JogCancel));
}

void GrblDriver::sendOverrideSteps(int current, int target, char plus10, char minus10, char plus1, char minus1)
{
    QByteArray seq;
    int diff = target - current;
    while (diff >= 10) {
        seq += plus10;
        diff -= 10;
    }
    while (diff <= -10) {
        seq += minus10;
        diff += 10;
    }
    while (diff > 0) {
        seq += plus1;
        --diff;
    }
    while (diff < 0) {
        seq += minus1;
        ++diff;
    }
    if (!seq.isEmpty())
        write(seq);
}

void GrblDriver::setFeedOverride(int percent)
{
    percent = qBound(10, percent, 200);
    if (percent == 100)
        write(QByteArray(1, FeedOv100));
    else
        sendOverrideSteps(m_expectedOvFeed, percent, FeedOvPlus10, FeedOvMinus10, FeedOvPlus1, FeedOvMinus1);
    m_expectedOvFeed = percent;
    m_sinceOverride.restart();
}

void GrblDriver::setSpindleOverride(int percent)
{
    percent = qBound(10, percent, 200);
    if (percent == 100)
        write(QByteArray(1, SpindleOv100));
    else
        sendOverrideSteps(m_expectedOvSpindle, percent, SpindleOvPlus10, SpindleOvMinus10, SpindleOvPlus1,
                          SpindleOvMinus1);
    m_expectedOvSpindle = percent;
    m_sinceOverride.restart();
}
