#include "grbl/simulatedgrbl.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace ld::grbl {

namespace {
constexpr int kPlannerSize = 15;
constexpr int kRxSize = 127;
}  // namespace

SimulatedGrbl::SimulatedGrbl(QObject* parent) : IPort(parent)
{
    const std::pair<int, const char*> defaults[] = {
        {0, "10"}, {1, "25"}, {2, "0"}, {3, "3"}, {4, "0"}, {5, "0"}, {6, "0"}, {10, "1"}, {11, "0.010"},
        {12, "0.002"}, {13, "0"}, {20, "0"}, {21, "0"}, {22, "0"}, {23, "0"}, {24, "25.000"},
        {25, "500.000"}, {26, "250"}, {27, "1.000"}, {30, "1000"}, {31, "0"}, {32, "1"}, {100, "80.000"},
        {101, "80.000"}, {102, "80.000"}, {110, "8000.000"}, {111, "8000.000"}, {112, "500.000"},
        {120, "200.000"}, {121, "200.000"}, {122, "10.000"}, {130, "2000.000"}, {131, "2000.000"},
        {132, "200.000"}};
    for (const auto& [number, value] : defaults) settings_[number] = QString::fromLatin1(value);
    timer_.setInterval(5);
    connect(&timer_, &QTimer::timeout, this, &SimulatedGrbl::tick);
}

bool SimulatedGrbl::open(QString*)
{
    if (open_) return true;
    open_ = true;
    booted_ = false;
    out_.clear();
    clock_.start();
    lastTickNs_ = 0;
    timer_.start();
    // Wie ein Arduino: Nach dem Öffnen startet GRBL neu und meldet sich.
    QTimer::singleShot(30, this, [this] {
        if (open_) {
            booted_ = true;
            reply(QStringLiteral("\r\nGrbl 1.1h ['$' for help]"));
        }
    });
    return true;
}

void SimulatedGrbl::close()
{
    open_ = false;
    timer_.stop();
}

void SimulatedGrbl::dropConnection()
{
    if (!open_) return;
    open_ = false;
    timer_.stop();
    emit failed(tr("Das Gerät wurde getrennt."));
}

QByteArray SimulatedGrbl::readAll()
{
    const QByteArray data = out_;
    out_.clear();
    return data;
}

void SimulatedGrbl::reply(const QString& text)
{
    if (silent_) return;
    out_ += text.toLatin1() + "\r\n";
    if (!notifyPending_) {
        notifyPending_ = true;
        QTimer::singleShot(0, this, [this] {
            notifyPending_ = false;
            if (open_ && !out_.isEmpty()) emit readyRead();
        });
    }
}

void SimulatedGrbl::write(const QByteArray& data)
{
    if (!open_ || !booted_) return;   // beim Starten hört GRBL noch nicht zu
    feed(data);
}

void SimulatedGrbl::feed(const QByteArray& bytes)
{
    for (const char c : bytes) {
        switch (c) {
        case '?': sendStatus(); continue;
        case '!': if (isMoving()) { hold_ = true; holdAge_ = 0.0; } continue;
        case '~': hold_ = false; continue;
        case '\x18': reset(isMoving()); continue;
        case '\x90': feedOv_ = 100; continue;
        case '\x91': feedOv_ = std::min(200, feedOv_ + 10); continue;
        case '\x92': feedOv_ = std::max(10, feedOv_ - 10); continue;
        case '\x93': feedOv_ = std::min(200, feedOv_ + 1); continue;
        case '\x94': feedOv_ = std::max(10, feedOv_ - 1); continue;
        case '\x99': spindleOv_ = 100; continue;
        case '\x9A': spindleOv_ = std::min(200, spindleOv_ + 10); continue;
        case '\x9B': spindleOv_ = std::max(10, spindleOv_ - 10); continue;
        case '\x9C': spindleOv_ = std::min(200, spindleOv_ + 1); continue;
        case '\x9D': spindleOv_ = std::max(10, spindleOv_ - 1); continue;
        case '\x85':
            jogs_.clear();
            if (current_ && current_->jog) current_.reset();
            continue;
        default: break;
        }
        ++rxBytes_;
        maxRx_ = std::max(maxRx_, rxBytes_);
        if (rxBytes_ > kRxSize) overflowed_ = true;
        if (c == '\n') {
            const int size = static_cast<int>(partial_.size()) + 1;
            rxLines_.emplace_back(QString::fromLatin1(partial_).trimmed(), size);
            partial_.clear();
        } else {
            partial_.append(c);
        }
    }
    processLines();
}

void SimulatedGrbl::processLines()
{
    while (!rxLines_.empty()) {
        if (!processLine(rxLines_.front().first)) break;
        rxBytes_ -= rxLines_.front().second;
        rxLines_.pop_front();
    }
}

bool SimulatedGrbl::processLine(const QString& line)
{
    if (line.isEmpty()) {
        reply(QStringLiteral("ok"));
        return true;
    }
    if (line.startsWith(QLatin1Char('$'))) {
        if (line == QLatin1String("$$")) {
            for (auto it = settings_.begin(); it != settings_.end(); ++it)
                reply(QStringLiteral("$%1=%2").arg(it.key()).arg(it.value()));
            reply(QStringLiteral("ok"));
        } else if (line == QLatin1String("$#")) {
            // Koordinatensysteme (G54 = der gesetzte Nullpunkt), Werkzeuglängenversatz und letzte Antastung
            reply(QStringLiteral("[G54:%1,%2,0.000]").arg(wco_.x(), 0, 'f', 3).arg(wco_.y(), 0, 'f', 3));
            for (const char* name : {"G55", "G56", "G57", "G58", "G59", "G28", "G30", "G92"})
                reply(QStringLiteral("[%1:0.000,0.000,0.000]").arg(QLatin1String(name)));
            reply(QStringLiteral("[TLO:0.000]"));
            reply(QStringLiteral("[PRB:0.000,0.000,0.000:0]"));
            reply(QStringLiteral("ok"));
        } else if (line == QLatin1String("$I")) {
            reply(QStringLiteral("[VER:1.1h.20190825:]"));
            reply(QStringLiteral("[OPT:V,15,128]"));
            reply(QStringLiteral("ok"));
        } else if (line == QLatin1String("$X")) {
            alarm_ = 0;
            reply(QStringLiteral("[MSG:Caution: Unlocked]"));
            reply(QStringLiteral("ok"));
        } else if (line.startsWith(QLatin1String("$J="))) {
            if (alarm_ > 0) { reply(QStringLiteral("error:9")); return true; }
            return processGcode(line.mid(3), true);
        } else if (line == QLatin1String("$H")) {
            reply(QStringLiteral("error:5"));
        } else {
            static const QRegularExpression setting(QStringLiteral("^\\$(\\d+)=(.+)$"));
            const auto m = setting.match(line);
            if (!m.hasMatch()) {
                reply(QStringLiteral("error:3"));
            } else if (!isIdle()) {
                reply(QStringLiteral("error:8"));
            } else {
                settings_[m.captured(1).toInt()] = m.captured(2);
                reply(QStringLiteral("ok"));
            }
        }
        return true;
    }
    if (alarm_ > 0) {
        reply(QStringLiteral("error:9"));
        return true;
    }
    return processGcode(line, false);
}

bool SimulatedGrbl::processGcode(const QString& original, bool jog)
{
    QString text = original.toUpper();
    text.remove(QRegularExpression(QStringLiteral("\\(.*?\\)")));
    const int semi = text.indexOf(QLatin1Char(';'));
    if (semi >= 0) text = text.left(semi);
    text.remove(QLatin1Char(' '));
    if (text.isEmpty()) {
        reply(QStringLiteral("ok"));
        return true;
    }

    static const QRegularExpression word(QStringLiteral("([A-Z])(-?[0-9]*\\.?[0-9]+)"));
    QList<QPair<QChar, double>> words;
    int covered = 0;
    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        words.append({m.captured(1).at(0), m.captured(2).toDouble()});
        covered += static_cast<int>(m.capturedLength());
    }
    if (covered != text.size()) {
        reply(QStringLiteral("error:1"));
        return true;
    }

    bool hasXY = false, hasG10 = false, spindleChange = false;
    std::optional<double> x, y, f, s, l, p;
    int g = -1;
    bool newRelative = relative_, newRapid = rapidMode_;
    int newSpindle = spindle_;
    for (const auto& [letter, value] : words) {
        const int number = static_cast<int>(std::lround(value * 10.0));   // G59.1 -> 591
        if (letter == QLatin1Char('G')) {
            if (number == 0) { newRapid = true; g = 0; }
            else if (number == 10 || number == 20 || number == 30) { newRapid = false; g = 1; }   // Bögen (G2/G3) fährt der Simulator vereinfacht als Gerade zum Ziel
            else if (number == 100) hasG10 = true;
            else if (number == 900) newRelative = false;
            else if (number == 910) newRelative = true;
            else if (number == 210 || number == 940 || number == 540) {}
            else { reply(QStringLiteral("error:20")); return true; }
        } else if (letter == QLatin1Char('M')) {
            if (number == 30) { newSpindle = 3; spindleChange = true; }
            else if (number == 40) { newSpindle = 4; spindleChange = true; }
            else if (number == 50) { newSpindle = 0; spindleChange = true; }
            else { reply(QStringLiteral("error:20")); return true; }
        } else if (letter == QLatin1Char('X')) { x = value; hasXY = true; }
        else if (letter == QLatin1Char('Y')) { y = value; hasXY = true; }
        else if (letter == QLatin1Char('Z')) { /* keine Z-Achse: ignoriert */ }
        else if (letter == QLatin1Char('F')) f = value;
        else if (letter == QLatin1Char('S')) { s = value; spindleChange = true; }
        else if (letter == QLatin1Char('I') || letter == QLatin1Char('J') || letter == QLatin1Char('K') || letter == QLatin1Char('R')) {}   // Bogenmittelpunkt/-radius: ohne Wirkung
        else if (letter == QLatin1Char('L')) l = value;
        else if (letter == QLatin1Char('P')) p = value;
        else { reply(QStringLiteral("error:20")); return true; }
    }
    (void)g;

    if (hasG10) {
        // G10 L20 P1 X.. Y..: wirkt erst, wenn alle Fahrten fertig sind.
        if (isMoving()) return false;
        if (!l || !p) { reply(QStringLiteral("error:28")); return true; }
        if (*l == 20.0 && *p == 1.0) {
            if (x) wco_.setX(mpos_.x() - *x);
            if (y) wco_.setY(mpos_.y() - *y);
        }
        if (!jog) { ++acceptedCount_; accepted_.append(original); }
        reply(QStringLiteral("ok"));
        return true;
    }

    const bool needsBlock = hasXY || (spindleChange && !jog);
    if (needsBlock && static_cast<int>(planner_.size()) >= kPlannerSize && !jog) return false;   // Planer voll

    if (!jog) {
        ++acceptedCount_;
        accepted_.append(original);
        if (acceptedCount_ == failAt_) {
            reply(QStringLiteral("error:%1").arg(failCode_));
            return true;
        }
    }
    if (jog) {
        if (!f || *f <= 0.0 || !hasXY) { reply(QStringLiteral("error:16")); return true; }
        Block b;
        b.jog = true;
        b.from = !jogs_.empty() ? jogs_.back().to : (current_ && current_->jog ? current_->to : mpos_);
        b.to = b.from;
        if (newRelative) {
            if (x) b.to.rx() += *x;
            if (y) b.to.ry() += *y;
        } else {
            if (x) b.to.setX(wco_.x() + *x);
            if (y) b.to.setY(wco_.y() + *y);
        }
        b.lengthMm = std::hypot(b.to.x() - b.from.x(), b.to.y() - b.from.y());
        b.feed = *f;
        jogs_.push_back(b);
        reply(QStringLiteral("ok"));
        return true;
    }

    relative_ = newRelative;
    rapidMode_ = newRapid;
    if (f) feedRate_ = *f;
    if (s) sValue_ = static_cast<int>(*s);
    spindle_ = newSpindle;
    if (hasXY) {
        if (!rapidMode_ && feedRate_ <= 0.0) { reply(QStringLiteral("error:22")); return true; }
        Block b;
        b.rapid = rapidMode_;
        b.from = programmed_;
        b.to = programmed_;
        if (relative_) {
            if (x) b.to.rx() += *x;
            if (y) b.to.ry() += *y;
        } else {
            if (x) b.to.setX(wco_.x() + *x);
            if (y) b.to.setY(wco_.y() + *y);
        }
        b.lengthMm = std::hypot(b.to.x() - b.from.x(), b.to.y() - b.from.y());
        b.feed = feedRate_;
        b.mode = spindle_;
        b.s = sValue_;
        programmed_ = b.to;
        planner_.append(b);
    } else if (spindleChange) {
        Block b;
        b.spindleOnly = true;
        b.mode = spindle_;
        b.s = sValue_;
        planner_.append(b);
    }
    reply(QStringLiteral("ok"));
    return true;
}

void SimulatedGrbl::tick()
{
    const qint64 now = clock_.nsecsElapsed();
    const double dt = static_cast<double>(now - lastTickNs_) / 1e9 * timeScale_;
    lastTickNs_ = now;
    advance(dt);
    processLines();
}

void SimulatedGrbl::advance(double seconds)
{
    if (alarm_ > 0) { laserOn_ = false; return; }
    if (hold_) {
        laserOn_ = false;   // Feed Hold schaltet den Laser aus
        holdAge_ += seconds;
        return;
    }
    const bool laserMode = settings_.value(32) == QLatin1String("1");
    double left = seconds;
    while (left > 0.0) {
        if (!current_) {
            if (!jogs_.empty()) {
                current_ = jogs_.front();
                jogs_.pop_front();
            } else if (!planner_.isEmpty()) {
                current_ = planner_.takeFirst();
            } else {
                break;
            }
            doneMm_ = 0.0;
            execMode_ = current_->mode;
            execS_ = current_->s;
            if (current_->spindleOnly) {
                current_.reset();
                continue;
            }
        }
        const Block& b = *current_;
        const double speed = (b.rapid ? settings_.value(110).toDouble() : b.feed * feedOv_ / 100.0) / 60.0;   // mm/s
        laserOn_ = !b.jog && execMode_ != 0 && execS_ > 0 && (laserMode ? !b.rapid : true);
        if (laserOn_) maxLaserS_ = std::max(maxLaserS_, effectiveS());
        const double remaining = b.lengthMm - doneMm_;
        const double need = speed > 0.0 ? remaining / speed : 0.0;
        if (left >= need) {
            mpos_ = b.to;
            minPos_ = QPointF(std::min(minPos_.x(), mpos_.x()), std::min(minPos_.y(), mpos_.y()));
            maxPos_ = QPointF(std::max(maxPos_.x(), mpos_.x()), std::max(maxPos_.y(), mpos_.y()));
            left -= need;
            current_.reset();
        } else {
            doneMm_ += speed * left;
            const double f = b.lengthMm > 0.0 ? doneMm_ / b.lengthMm : 1.0;
            mpos_ = b.from + (b.to - b.from) * f;
            left = 0.0;
        }
    }
    if (!current_) laserOn_ = false;
}

QString SimulatedGrbl::stateText() const
{
    if (alarm_ > 0) return QStringLiteral("Alarm");
    if (hold_) return holdAge_ < 0.2 ? QStringLiteral("Hold:0") : QStringLiteral("Hold:1");
    if (current_ ? current_->jog : !jogs_.empty()) return QStringLiteral("Jog");
    if (isMoving()) return QStringLiteral("Run");
    return QStringLiteral("Idle");
}

void SimulatedGrbl::sendStatus()
{
    if (silent_) return;
    const int feedNow = current_ && !hold_ ? static_cast<int>(current_->rapid ? settings_.value(110).toDouble() : current_->feed) : 0;
    QString text = QStringLiteral("<%1|MPos:%2,%3,0.000|Bf:%4,%5|FS:%6,%7")
                       .arg(stateText())
                       .arg(mpos_.x(), 0, 'f', 3)
                       .arg(mpos_.y(), 0, 'f', 3)
                       .arg(kPlannerSize - static_cast<int>(planner_.size()))
                       .arg(std::max(0, kRxSize - rxBytes_))
                       .arg(feedNow)
                       .arg(laserOn_ ? effectiveS() : 0);
    if (statusCounter_ % 10 == 5) text += QStringLiteral("|Ov:%1,100,%2").arg(feedOv_).arg(spindleOv_);
    if (statusCounter_++ % 10 == 0) {
        text += QStringLiteral("|WCO:%1,%2,0.000").arg(wco_.x(), 0, 'f', 3).arg(wco_.y(), 0, 'f', 3);
    }
    reply(text + QLatin1Char('>'));
}

void SimulatedGrbl::reset(bool moving)
{
    ++resets_;
    planner_.clear();
    jogs_.clear();
    current_.reset();
    rxLines_.clear();
    partial_.clear();
    rxBytes_ = 0;
    hold_ = false;
    feedOv_ = spindleOv_ = 100;
    laserOn_ = false;
    execMode_ = 0;
    spindle_ = 0;
    programmed_ = mpos_;
    alarm_ = moving ? 3 : 0;
    reply(QStringLiteral("\r\nGrbl 1.1h ['$' for help]"));
    if (moving) reply(QStringLiteral("ALARM:3"));
}

void SimulatedGrbl::setAlarm(int code)
{
    alarm_ = code;
    planner_.clear();
    jogs_.clear();
    current_.reset();
    laserOn_ = false;
    reply(QStringLiteral("ALARM:%1").arg(code));
}

void SimulatedGrbl::injectRaw(const QByteArray& bytes)
{
    if (!open_) return;
    out_ += bytes;
    emit readyRead();
}

void SimulatedGrbl::injectAlarm(int code)
{
    setAlarm(code);
}

void SimulatedGrbl::failLine(int acceptedLineNumber, int errorCode)
{
    failAt_ = acceptedLineNumber;
    failCode_ = errorCode;
}

}  // namespace ld::grbl
