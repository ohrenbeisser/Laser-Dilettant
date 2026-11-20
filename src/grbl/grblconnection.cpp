#include "grbl/grblconnection.h"

#include "grbl/clock.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

namespace ld::grbl {

namespace {

constexpr int kPollMs = 200;          // 5 Statusabfragen pro Sekunde
constexpr qint64 kSilentMs = 3000;    // so lange ohne Antwort gilt GRBL als stumm
constexpr qint64 kConnectTimeoutMs = 6000;

}  // namespace

GrblConnection::GrblConnection(QObject* parent) : QObject(parent)
{
    pollTimer_.setInterval(kPollMs);
    connect(&pollTimer_, &QTimer::timeout, this, &GrblConnection::pollTick);
}

GrblConnection::~GrblConnection()
{
    if (port_ && port_->isOpen()) {
        // Der Laser wird auch beim Zerstören ausgeschaltet (Reset und M5). Es meldet aber niemand mehr etwas:
        // Empfänger wie das Hauptfenster können dann schon halb abgebaut sein.
        blockSignals(true);
        disconnectDevice();
    }
}

void GrblConnection::setPort(IPort* port)
{
    if (port_) {
        if (port_->isOpen()) disconnectDevice();
        port_->deleteLater();
        port_ = nullptr;
    }
    port_ = port;
    if (port_) {
        port_->setParent(this);
        connect(port_, &IPort::readyRead, this, &GrblConnection::onReadyRead);
        connect(port_, &IPort::failed, this, &GrblConnection::onPortFailed);
    }
}

bool GrblConnection::connectDevice(QString* error)
{
    if (!port_ || state_ != State::Disconnected) {
        if (error) *error = tr("Es ist schon eine Verbindung aktiv.");
        return false;
    }
    QString reason;
    if (!port_->open(&reason)) {
        if (error) *error = reason;
        return false;
    }
    rxBuffer_.clear();
    spindleLimitReported_ = false;
    maxSpindle_ = 0;   // $30 gilt nur für die Verbindung, in der es gelesen wurde
    clearTransfers();
    status_ = GrblStatus{};
    wco_ = QPointF();
    silent_ = false;
    connectStartMs_ = lastResponseMs_ = monotonicMs();
    setState(State::Connecting);
    pollTimer_.start();
    sendRealtime('?');
    return true;
}

void GrblConnection::disconnectDevice()
{
    if (!port_ || state_ == State::Disconnected) return;
    pollTimer_.stop();
    selfResetMs_ = monotonicMs();
    if (port_->isOpen()) {
        // Sicherheit zuerst: Der Laser muss aus sein. Reset, kurz warten (GRBL leert dabei seinen
        // Empfangspuffer), dann M5.
        // Ohne Ereignisschleife (kein Klick, keine Antwort darf dazwischenkommen): schreiben, auf die Leitung warten, pausieren.
        port_->write(QByteArray(1, '\x18'));
        port_->flush(300);
        QThread::msleep(100);
        port_->write("M5\n");
        port_->flush(300);
        QThread::msleep(40);
        port_->close();
    }
    const bool wasActive = jobActive_;
    clearTransfers();
    setState(State::Disconnected);
    if (wasActive) emit jobAborted(tr("Die Verbindung wurde getrennt."));
}

void GrblConnection::onPortFailed(const QString& reason)
{
    if (state_ == State::Disconnected) return;
    pollTimer_.stop();
    if (port_ && port_->isOpen()) port_->close();
    const bool wasActive = jobActive_;
    clearTransfers();
    setState(State::Disconnected);
    if (wasActive) emit jobAborted(tr("Die Verbindung ist abgebrochen."));
    emit connectionLost(reason);
}

void GrblConnection::setState(State state)
{
    if (state == state_) return;
    state_ = state;
    emit stateChanged(state);
}

void GrblConnection::clearTransfers()
{
    inFlight_.clear();
    inFlightBytes_ = 0;
    commands_.clear();
    jobActive_ = false;
    jobHeld_ = false;
    jobAllSent_ = false;
    hasJobPending_ = false;
    if (jobFile_.isOpen()) jobFile_.close();
}

void GrblConnection::sendRealtime(char byte)
{
    if (port_ && port_->isOpen()) port_->write(QByteArray(1, byte));
}

bool GrblConnection::sendLine(const QString& line)
{
    if (state_ != State::Connected || jobActive_) return false;
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return false;
    commands_.push_back(trimmed);
    pump();
    return true;
}

bool GrblConnection::readSettings()
{
    collectedSettings_.clear();
    return sendLine(QStringLiteral("$$"));
}

bool GrblConnection::readParameters()
{
    collectedParams_.clear();
    return sendLine(QStringLiteral("$#"));
}

bool GrblConnection::readInfo()
{
    versionLine_.clear();
    optionsLine_.clear();
    return sendLine(QStringLiteral("$I"));
}

void GrblConnection::sendOverrideSteps(int from, int to, bool spindle)
{
    // Echtzeitbefehle für Vorschub: 0x91 +10, 0x92 -10, 0x93 +1, 0x94 -1; für Leistung: 0x9A, 0x9B, 0x9C, 0x9D.
    const char plus10 = spindle ? '\x9A' : '\x91', minus10 = spindle ? '\x9B' : '\x92';
    const char plus1 = spindle ? '\x9C' : '\x93', minus1 = spindle ? '\x9D' : '\x94';
    int diff = to - from;
    while (diff >= 10) { sendRealtime(plus10); diff -= 10; }
    while (diff <= -10) { sendRealtime(minus10); diff += 10; }
    while (diff >= 1) { sendRealtime(plus1); --diff; }
    while (diff <= -1) { sendRealtime(minus1); ++diff; }
    lastOverrideSendMs_ = monotonicMs();
}

void GrblConnection::setFeedOverride(int percent)
{
    percent = std::clamp(percent, 10, 200);
    if (percent == feedTarget_ && percent == feedApplied_) return;
    sendOverrideSteps(feedApplied_, percent, false);
    feedTarget_ = feedApplied_ = percent;
    emit overridesChanged(feedTarget_, spindleTarget_);
}

void GrblConnection::setSpindleOverride(int percent)
{
    percent = std::clamp(percent, 10, 200);   // GRBL kappt die Leistung selbst bei $30
    if (percent == spindleTarget_ && percent == spindleApplied_) return;
    sendOverrideSteps(spindleApplied_, percent, true);
    spindleTarget_ = spindleApplied_ = percent;
    emit overridesChanged(feedTarget_, spindleTarget_);
}

void GrblConnection::resetOverrides()
{
    sendRealtime('\x90');
    sendRealtime('\x99');
    lastOverrideSendMs_ = monotonicMs();
    const bool changed = feedTarget_ != 100 || spindleTarget_ != 100;
    feedTarget_ = feedApplied_ = spindleTarget_ = spindleApplied_ = 100;
    if (changed) emit overridesChanged(100, 100);
}

void GrblConnection::reconcileOverrides(const GrblStatus& status)
{
    // GRBL meldet die Overrides nur gelegentlich. Weicht der gemeldete Wert von dem ab, den wir eingestellt
    // haben (zum Beispiel weil ein Echtzeitbefehl verloren ging), wird nachgeregelt.
    if (monotonicMs() - lastOverrideSendMs_ < 1500) return;
    if (status.ovFeed >= 0 && status.ovFeed != feedTarget_) {
        sendOverrideSteps(status.ovFeed, feedTarget_, false);
        feedApplied_ = feedTarget_;
    }
    if (status.ovSpindle >= 0 && status.ovSpindle != spindleTarget_) {
        sendOverrideSteps(status.ovSpindle, spindleTarget_, true);
        spindleApplied_ = spindleTarget_;
    }
}

void GrblConnection::feedHold() { sendRealtime('!'); }
void GrblConnection::resume() { sendRealtime('~'); }
void GrblConnection::jogCancel() { sendRealtime('\x85'); }

void GrblConnection::softReset()
{
    selfResetMs_ = monotonicMs();
    sendRealtime('\x18');
    feedTarget_ = feedApplied_ = spindleTarget_ = spindleApplied_ = 100;   // ein Reset setzt die Overrides zurück
    emit overridesChanged(100, 100);
    emit resetSent();
    // Alles, was GRBL noch im Puffer hatte, ist weg; die Warteschlange gilt nicht mehr.
    const bool wasActive = jobActive_;
    inFlight_.clear();
    inFlightBytes_ = 0;
    commands_.clear();
    jobActive_ = false;
    jobHeld_ = false;
    hasJobPending_ = false;
    if (jobFile_.isOpen()) jobFile_.close();
    if (wasActive) emit jobAborted(tr("Der Auftrag wurde durch einen Reset abgebrochen."));
}

void GrblConnection::emergencyStop()
{
    m5AfterReset_ = true;
    positionUncertain_ = true;
    softReset();
}

void GrblConnection::cancelJob()
{
    // Der Bediener hat den Abbruch selbst bestätigt: Den Alarm, den GRBL nach dem Reset in der Fahrt meldet,
    // hebt die Verbindung selbst auf. (Not-Aus und echte Alarme tun das nicht.)
    autoUnlockUntilMs_ = monotonicMs() + 5000;
    m5AfterReset_ = true;
    positionUncertain_ = true;
    softReset();
}

bool GrblConnection::autoUnlockPending() const
{
    return monotonicMs() < autoUnlockUntilMs_;
}

bool GrblConnection::startJob(const QString& path, QString* error)
{
    if (state_ != State::Connected || jobActive_) {
        if (error) *error = tr("Es läuft schon ein Auftrag oder es besteht keine Verbindung.");
        return false;
    }
    jobFile_.setFileName(path);
    if (!jobFile_.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("Die G-Code-Datei konnte nicht geöffnet werden.");
        return false;
    }
    jobSize_ = std::max<qint64>(1, jobFile_.size());
    spindleLimitReported_ = false;
    jobActive_ = true;
    jobHeld_ = false;
    jobAllSent_ = false;
    jobLineNumber_ = 0;
    jobAcked_ = 0;
    hasJobPending_ = false;
    pump();
    return true;
}

void GrblConnection::continueJob()
{
    if (!jobActive_ || !jobHeld_) return;
    jobHeld_ = false;
    resume();
    pump();
}

double GrblConnection::jobFraction() const
{
    if (!jobActive_) return 0.0;
    if (jobAllSent_) return 1.0;
    return std::min(1.0, static_cast<double>(jobFile_.pos()) / static_cast<double>(jobSize_));
}

bool GrblConnection::nextJobLine(QString& line, long long& number)
{
    if (hasJobPending_) {
        line = jobPendingLine_;
        number = jobPendingNumber_;
        return true;
    }
    while (!jobFile_.atEnd()) {
        const QByteArray raw = jobFile_.readLine();
        ++jobLineNumber_;
        const QString text = QString::fromLatin1(raw).trimmed();
        // Leere Zeilen und reine Kommentare brauchen GRBL nicht zu belasten.
        if (text.isEmpty() || text.startsWith(QLatin1Char('(')) || text.startsWith(QLatin1Char(';'))) continue;
        jobPendingLine_ = line = text;
        jobPendingNumber_ = number = jobLineNumber_;
        hasJobPending_ = true;
        return true;
    }
    return false;
}

void GrblConnection::pump()
{
    if (!port_ || !port_->isOpen() || state_ != State::Connected) return;
    for (;;) {
        QString text;
        long long jobLine = -1;
        bool fromJob = false;
        if (!commands_.empty()) {
            text = commands_.front();
        } else if (jobActive_ && !jobHeld_ && nextJobLine(text, jobLine)) {
            fromJob = true;
        } else {
            if (jobActive_ && !jobHeld_ && !hasJobPending_ && jobFile_.atEnd()) jobAllSent_ = true;
            break;
        }
        // Nie mehr Leistung anfordern, als GRBL kennt ($30).
        double requested = 0.0;
        const QString limited = limitSpindleWords(text, maxSpindle_, &requested);
        if (requested > 0.0 && !spindleLimitReported_) {
            spindleLimitReported_ = true;
            emit spindleLimited(static_cast<int>(requested), maxSpindle_);
        }
        text = limited;
        const QByteArray bytes = text.toLatin1() + '\n';
        const int length = static_cast<int>(bytes.size());
        if (inFlightBytes_ > 0 && inFlightBytes_ + length > kRxLimit) break;   // wartet auf "ok"
        port_->write(bytes);
        inFlight_.push_back({length, jobLine, text});
        inFlightBytes_ += length;
        if (fromJob) {
            hasJobPending_ = false;
        } else {
            commands_.pop_front();
            emit lineSent(text);
        }
    }
}

void GrblConnection::onReadyRead()
{
    rxBuffer_ += port_->readAll();
    for (;;) {
        const int nl = rxBuffer_.indexOf('\n');
        if (nl < 0) break;
        const QString line = QString::fromLatin1(rxBuffer_.left(nl)).trimmed();
        rxBuffer_.remove(0, nl + 1);
        if (!line.isEmpty()) handleLine(line);
    }
    // Rauschen oder falsche Baudrate: Daten ohne Zeilenende dürfen den Speicher nicht füllen.
    if (rxBuffer_.size() > kMaxLineBytes) rxBuffer_.clear();
}

void GrblConnection::handleLine(const QString& line)
{
    lastResponseMs_ = monotonicMs();
    if (silent_) {
        silent_ = false;
        emit unresponsive(false);
    }
    emit lineReceived(line);

    if (line.startsWith(QLatin1Char('<'))) {
        if (const auto parsed = parseStatusLine(line, wco_)) {
            status_ = *parsed;
            reconcileOverrides(status_);
            if (state_ == State::Connecting) setState(State::Connected);
            emit statusChanged(status_);
            if (jobActive_) {
                emit jobProgress(jobFraction());
                // Fertig, wenn alles gesendet und bestätigt ist und GRBL wieder bereit ist.
                if (jobAllSent_ && inFlight_.empty() && status_.state == MachineState::Idle) {
                    jobActive_ = false;
                    if (jobFile_.isOpen()) jobFile_.close();
                    emit jobProgress(1.0);
                    emit jobFinished();
                }
            }
            if (m5AfterReset_ && status_.state == MachineState::Idle) {
                m5AfterReset_ = false;
                autoUnlockUntilMs_ = kLongAgoMs;   // kein Alarm entstanden (Abbruch im Stillstand)
                commands_.push_back(QStringLiteral("M5"));
                pump();
            } else if (m5AfterReset_ && status_.state == MachineState::Alarm) {
                if (autoUnlockPending()) {
                    // Regulärer Abbruch: Alarm aufheben; das M5 folgt, sobald GRBL wieder bereit ist.
                    autoUnlockUntilMs_ = kLongAgoMs;
                    commands_.push_back(QStringLiteral("$X"));
                    pump();
                } else {
                    m5AfterReset_ = false;   // im Alarm ist der Laser von GRBL aus; G-Code wäre gesperrt
                }
            }
        }
        return;
    }
    if (line.startsWith(QLatin1Char('$'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        bool ok = false;
        const int number = eq > 1 ? line.mid(1, eq - 1).toInt(&ok) : 0;
        if (ok) {
            collectedSettings_[number] = line.mid(eq + 1).trimmed();
            learnSetting(number, collectedSettings_[number]);
        }
    } else if (line.startsWith(QLatin1String("[VER:"))) {
        versionLine_ = line.mid(5, line.size() - 6);
    } else if (line.startsWith(QLatin1String("[OPT:"))) {
        optionsLine_ = line.mid(5, line.size() - 6);
    } else if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
        // Antwort auf $#: [G54:0.000,0.000,0.000], [TLO:0.000], [PRB:0.000,0.000,0.000:0] ...
        static const QRegularExpression parameter(QStringLiteral("^\\[(G5[4-9]|G28|G30|G92|TLO|PRB):"));
        if (parameter.match(line).hasMatch()) collectedParams_ << line.mid(1, line.size() - 2);
    }
    if (line == QLatin1String("ok")) {
        handleAck(true, 0);
    } else if (line.startsWith(QLatin1String("error:"))) {
        handleAck(false, line.mid(6).toInt());
    } else if (line.startsWith(QLatin1String("ALARM:"))) {
        handleAlarm(line.mid(6).toInt());
    } else if (line.startsWith(QLatin1String("Grbl "))) {
        onWelcome(line);
    }
}

void GrblConnection::learnSetting(int number, const QString& value)
{
    if (number != 30) return;
    bool ok = false;
    const double s = value.toDouble(&ok);
    if (ok && s > 0.0) maxSpindle_ = static_cast<int>(s);
}

void GrblConnection::onWelcome(const QString& text)
{
    // GRBL meldet sich nach jedem Reset. Alles Unbestätigte ist damit verloren.
    feedTarget_ = feedApplied_ = spindleTarget_ = spindleApplied_ = 100;
    const bool wasActive = jobActive_;
    inFlight_.clear();
    inFlightBytes_ = 0;
    if (wasActive) {
        // Ein Reset, den wir nicht ausgelöst haben (zum Beispiel Stromausfall des Lasers).
        clearTransfers();
        emit jobAborted(tr("GRBL wurde zurückgesetzt."));
        positionUncertain_ = true;
    }
    if (state_ == State::Connecting) setState(State::Connected);
    emit welcome(text);
}

void GrblConnection::handleAck(bool ok, int code)
{
    if (inFlight_.empty()) return;   // zum Beispiel "ok" eines Befehls aus der Zeit vor einem Reset
    const InFlight item = inFlight_.front();
    inFlight_.pop_front();
    inFlightBytes_ -= item.length;
    if (ok) {
        if (item.jobLine >= 0) ++jobAcked_;
        // Hat der Bediener $30 selbst geändert, gilt ab jetzt der neue Wert.
        if (item.text.startsWith(QLatin1String("$30="))) learnSetting(30, item.text.mid(4).trimmed());
        if (item.text == QLatin1String("$$")) emit settingsRead(collectedSettings_);
        if (item.text == QLatin1String("$I")) emit infoRead(versionLine_, optionsLine_);
        if (item.text == QLatin1String("$#")) emit parametersRead(collectedParams_);
    } else if (item.jobLine >= 0) {
        // Fehler im Auftrag: sofort anhalten (Feed Hold) und nichts mehr senden.
        jobHeld_ = true;
        feedHold();
        emit jobError(item.jobLine, code, errorText(code), item.text);
        return;
    } else {
        emit commandError(code, errorText(code), item.text);
    }
    pump();
}

void GrblConnection::handleAlarm(int code)
{
    // Ein Reset während der Fahrt (Not-Aus) löst bei GRBL Alarm 3 aus. Den haben wir selbst verursacht.
    if (code == 3 && monotonicMs() - selfResetMs_ < 3000) {
        positionUncertain_ = true;
        return;
    }
    const bool wasActive = jobActive_;
    // Bei einem Alarm muss der Laser sofort aus: Reset und Auftrag verwerfen.
    emergencyStop();
    emit alarm(code, alarmText(code), wasActive);
}

void GrblConnection::pollTick()
{
    if (!port_ || !port_->isOpen()) return;
    const qint64 now = monotonicMs();
    if (state_ == State::Connecting && now - connectStartMs_ > kConnectTimeoutMs) {
        pollTimer_.stop();
        port_->close();
        setState(State::Disconnected);
        emit connectFailed(tr("GRBL antwortet nicht. Port, Baudrate und Kabel prüfen."));
        return;
    }
    if (state_ == State::Connected && !silent_ && now - lastResponseMs_ > kSilentMs) {
        silent_ = true;
        emit unresponsive(true);
    }
    sendRealtime('?');
}

}  // namespace ld::grbl
