#include "device/SimulatedGrbl.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTickMs = 10;
constexpr int kMaxLineLength = 80;

QString fmt3(double v)
{
    if (std::abs(v) < 0.0005)
        v = 0.0;
    return QString::number(v, 'f', 3);
}

QString vec(const Vec3& v)
{
    return fmt3(v.x) + QLatin1Char(',') + fmt3(v.y) + QLatin1Char(',') + fmt3(v.z);
}

bool isRealtime(unsigned char c)
{
    return c == '?' || c == '!' || c == '~' || c == 0x18 || c >= 0x80;
}

// Strips whitespace and comments and converts to upper case, like GRBL's
// protocol layer does before the line reaches the parser.
QString cleanLine(const QByteArray& raw)
{
    QString out;
    bool paren = false;
    for (char ch : raw) {
        const QChar c = QLatin1Char(ch);
        if (paren) {
            if (c == QLatin1Char(')'))
                paren = false;
            continue;
        }
        if (c == QLatin1Char('('))
            paren = true;
        else if (c == QLatin1Char(';'))
            break;
        else if (!c.isSpace())
            out += c.toUpper();
    }
    return out;
}

} // namespace

SimulatedGrbl::SimulatedGrbl(QObject* parent)
    : ITransport(parent)
{
    loadDefaultSettings();
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SimulatedGrbl::tick);
}

SimulatedGrbl::~SimulatedGrbl() = default;

void SimulatedGrbl::loadDefaultSettings()
{
    // Typical values of a small diode laser with GRBL 1.1h.
    static const char* const defaults[][2] = {
        { "0", "10" },        { "1", "25" },       { "2", "0" },         { "3", "0" },
        { "4", "0" },         { "5", "0" },        { "6", "0" },         { "10", "1" },
        { "11", "0.010" },    { "12", "0.002" },   { "13", "0" },        { "20", "0" },
        { "21", "0" },        { "22", "0" },       { "23", "0" },        { "24", "25.000" },
        { "25", "500.000" },  { "26", "250" },     { "27", "1.000" },    { "30", "1000" },
        { "31", "0" },        { "32", "1" },       { "100", "80.000" },  { "101", "80.000" },
        { "102", "250.000" }, { "110", "6000.000" }, { "111", "6000.000" }, { "112", "500.000" },
        { "120", "500.000" }, { "121", "500.000" }, { "122", "10.000" }, { "130", "400.000" },
        { "131", "500.000" }, { "132", "200.000" },
    };
    m_settings.clear();
    for (const auto& d : defaults)
        m_settings.set(QString::fromLatin1(d[0]).toInt(), QString::fromLatin1(d[1]));
}

bool SimulatedGrbl::open(QString*)
{
    m_open = true;
    m_booted = false;
    m_inWire.clear();
    m_outWire.clear();
    m_rx.clear();
    m_planner.clear();
    m_mpos = m_plannedPos = Vec3 {};
    m_g92 = Vec3 {};
    m_velocity = 0;
    m_feedHold = false;
    m_homingPending = false;
    m_inBudget = m_outBudget = 0;
    m_gcodeLineCount = 0;
    m_statusRequested = false;
    m_clock.start();
    m_lastTick = 0;
    m_bootAt = m_bootDelayMs;
    if (!m_resetOnOpen) {
        // The controller was already running: no banner.
        m_booted = true;
        m_state = MachineState::Idle;
    }
    m_timer.start();
    return true;
}

void SimulatedGrbl::close()
{
    m_open = false;
    m_timer.stop();
    m_inWire.clear();
    m_outWire.clear();
}

QString SimulatedGrbl::description() const
{
    return QStringLiteral("Simulator");
}

void SimulatedGrbl::write(const QByteArray& data)
{
    if (m_open)
        m_inWire.append(data);
}

void SimulatedGrbl::simulateCableLoss()
{
    close();
    emit errorOccurred(QStringLiteral("Simulated cable loss"), true);
}

bool SimulatedGrbl::isMoving() const
{
    return !m_planner.empty() || m_velocity > 0 || m_homingPending;
}

bool SimulatedGrbl::laserFiring() const
{
    const bool moving = (m_state == MachineState::Run || m_state == MachineState::Jog) && !m_planner.empty()
        && m_planner.front().kind == Block::Move && m_velocity > 0;
    const Spindle sp = moving ? m_planner.front().spindle : m_spindle;
    const double s = moving ? m_planner.front().s : m_spindleS;
    if (sp == Spindle::Off || s <= 0)
        return false;
    if (m_settings.number(32) < 0.5)
        return true; // spindle mode: on as soon as M3/M4 is active
    if (m_feedHold)
        return false;
    if (moving)
        return !m_planner.front().rapid;
    // Laser mode at standstill: M3 (constant power) stays on while the modal
    // motion mode is G1/G2/G3, M4 (dynamic) is off without motion.
    return sp == Spindle::M3 && m_motionMode >= 1 && m_motionMode <= 3;
}

void SimulatedGrbl::send(const QString& text)
{
    m_outWire.append(text.toUtf8());
}

void SimulatedGrbl::sendLine(const QString& line)
{
    send(line + QStringLiteral("\r\n"));
}

void SimulatedGrbl::boot()
{
    m_booted = true;
    m_state = MachineState::Idle;
    send(QStringLiteral("\r\n") + m_banner + QStringLiteral("\r\n"));
    if (m_settings.number(22) > 0.5) {
        m_state = MachineState::Alarm;
        sendLine(QStringLiteral("[MSG:'$H'|'$X' to unlock]"));
    }
}

void SimulatedGrbl::softReset()
{
    // A reset after a completed feed hold (Hold:0) keeps the position; a
    // reset while the axes move does not.
    const bool wasMoving = (isMoving() && (m_state == MachineState::Run || m_state == MachineState::Jog
                                           || m_state == MachineState::Home))
        || (m_state == MachineState::Hold && m_velocity > 0);
    const bool wasHoming = m_homingPending;
    ++m_resetCount;
    m_rx.clear();
    m_planner.clear();
    m_homingPending = false;
    m_velocity = 0;
    m_dwellLeft = 0;
    m_plannedPos = m_mpos;
    m_feedHold = false;
    m_checkMode = false;
    m_absolute = true;
    m_inches = false;
    m_motionMode = 0;
    m_feed = 0;
    m_spindle = Spindle::Off;
    m_spindleS = 0;
    m_coolant = 9;
    m_ovFeed = m_ovRapid = m_ovSpindle = 100;
    m_ovChanged = m_wcoChanged = true;
    m_statusRequested = false;
    m_gcodeLineCount = 0;

    bool alarm = m_state == MachineState::Alarm;
    if (wasHoming) {
        sendLine(QStringLiteral("ALARM:6"));
        alarm = true;
    } else if (wasMoving) {
        sendLine(QStringLiteral("ALARM:3"));
        alarm = true;
    }
    send(QStringLiteral("\r\n") + m_banner + QStringLiteral("\r\n"));
    if (alarm || m_settings.number(22) > 0.5) {
        m_state = MachineState::Alarm;
        sendLine(QStringLiteral("[MSG:'$H'|'$X' to unlock]"));
    } else {
        m_state = MachineState::Idle;
    }
}

void SimulatedGrbl::handleRealtime(char ch)
{
    const auto c = static_cast<unsigned char>(ch);
    auto clampOv = [this](int& ov, int delta) {
        ov = std::clamp(ov + delta, 10, 200);
        m_ovChanged = true;
    };
    switch (c) {
    case '?':
        m_statusRequested = true;
        break;
    case '!':
        if (m_state == MachineState::Run) {
            m_feedHold = true;
            m_state = MachineState::Hold;
            m_holdSub = 1;
        } else if (m_state == MachineState::Jog) {
            handleRealtime(static_cast<char>(0x85));
        }
        break;
    case '~':
        if (m_state == MachineState::Hold) {
            m_feedHold = false;
            m_state = m_planner.empty() ? MachineState::Idle : MachineState::Run;
        }
        break;
    case 0x18:
        softReset();
        break;
    case 0x85: // jog cancel
        if (m_state == MachineState::Jog) {
            m_planner.clear();
            m_velocity = 0;
            m_plannedPos = m_mpos;
            m_state = MachineState::Idle;
        }
        break;
    case 0x90: m_ovFeed = 100; m_ovChanged = true; break;
    case 0x91: clampOv(m_ovFeed, 10); break;
    case 0x92: clampOv(m_ovFeed, -10); break;
    case 0x93: clampOv(m_ovFeed, 1); break;
    case 0x94: clampOv(m_ovFeed, -1); break;
    case 0x95: m_ovRapid = 100; m_ovChanged = true; break;
    case 0x96: m_ovRapid = 50; m_ovChanged = true; break;
    case 0x97: m_ovRapid = 25; m_ovChanged = true; break;
    case 0x99: m_ovSpindle = 100; m_ovChanged = true; break;
    case 0x9A: clampOv(m_ovSpindle, 10); break;
    case 0x9B: clampOv(m_ovSpindle, -10); break;
    case 0x9C: clampOv(m_ovSpindle, 1); break;
    case 0x9D: clampOv(m_ovSpindle, -1); break;
    default:
        break; // other real-time commands are ignored
    }
}

void SimulatedGrbl::receiveByte(char c)
{
    if (isRealtime(static_cast<unsigned char>(c))) {
        handleRealtime(c);
        return;
    }
    // GRBL's ring buffer of 128 bytes holds at most 127 characters.
    if (m_rx.size() >= kRxBufferSize - 1) {
        m_rxOverflow = true;
        return;
    }
    m_rx.append(c);
    m_maxRxUsage = std::max(m_maxRxUsage, static_cast<int>(m_rx.size()));
}

void SimulatedGrbl::tick()
{
    if (!m_open)
        return;
    const qint64 now = m_clock.elapsed();
    const double dt = (now - m_lastTick) / 1000.0;
    m_lastTick = now;

    if (!m_booted) {
        m_inWire.clear(); // the boot loader swallows everything
        if (now >= m_bootAt)
            boot();
        else
            return;
    }

    // PC -> controller
    qsizetype nIn = m_inWire.size();
    if (m_baud > 0) {
        m_inBudget += m_baud / 10.0 * dt;
        nIn = std::min<qsizetype>(nIn, static_cast<qsizetype>(m_inBudget));
        m_inBudget -= static_cast<double>(nIn);
    }
    if (nIn > 0) {
        const QByteArray chunk = m_inWire.left(nIn);
        m_inWire.remove(0, nIn);
        if (!m_hung)
            for (char c : chunk)
                receiveByte(c);
    }
    if (m_inWire.isEmpty())
        m_inBudget = 0;

    if (!m_hung) {
        processLines();
        advanceMotion(dt * m_timeScale);
        if (m_statusRequested) {
            m_statusRequested = false;
            sendStatus();
        }
    }

    // controller -> PC
    qsizetype nOut = m_outWire.size();
    if (m_baud > 0) {
        m_outBudget += m_baud / 10.0 * dt;
        nOut = std::min<qsizetype>(nOut, static_cast<qsizetype>(m_outBudget));
        m_outBudget -= static_cast<double>(nOut);
    }
    if (nOut > 0) {
        const QByteArray chunk = m_outWire.left(nOut);
        m_outWire.remove(0, nOut);
        emit dataReceived(chunk);
    }
    if (m_outWire.isEmpty())
        m_outBudget = 0;
}

void SimulatedGrbl::processLines()
{
    while (!m_homingPending && !plannerFull()) {
        const qsizetype nl = m_rx.indexOf('\n');
        const qsizetype cr = m_rx.indexOf('\r');
        qsizetype end = nl;
        if (cr >= 0 && (end < 0 || cr < end))
            end = cr;
        if (end < 0)
            return;
        const QByteArray raw = m_rx.left(end);
        m_rx.remove(0, end + 1);
        if (raw.isEmpty())
            continue; // "\r\n": second terminator, nothing to answer
        const QString line = cleanLine(raw);
        if (m_receivedLines.size() > 10000)
            m_receivedLines.removeFirst();
        m_receivedLines << line;
        QString reply;
        if (raw.size() > kMaxLineLength)
            reply = QStringLiteral("error:11");
        else if (line.isEmpty())
            reply = QStringLiteral("ok");
        else
            reply = executeLine(line);
        if (!reply.isEmpty())
            sendLine(reply);
    }
}

QString SimulatedGrbl::executeLine(const QString& line)
{
    if (line.startsWith(QLatin1Char('$')))
        return executeSystemCommand(line);
    if (m_state == MachineState::Alarm || m_state == MachineState::Jog)
        return QStringLiteral("error:9");
    return executeGcode(line, false);
}

QString SimulatedGrbl::executeSystemCommand(const QString& line)
{
    const bool cycleActive = m_state == MachineState::Run || m_state == MachineState::Hold
        || m_state == MachineState::Jog || m_state == MachineState::Home;

    if (line == QLatin1String("$")) {
        sendLine(QStringLiteral("[HLP:$$ $# $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H ~ ! ? ctrl-x]"));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$$")) {
        if (cycleActive)
            return QStringLiteral("error:8");
        for (auto it = m_settings.values().cbegin(); it != m_settings.values().cend(); ++it)
            sendLine(QStringLiteral("$%1=%2").arg(it.key()).arg(it.value()));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$#")) {
        sendLine(QStringLiteral("[G54:%1]").arg(vec(m_wco)));
        for (int g = 55; g <= 59; ++g)
            sendLine(QStringLiteral("[G%1:0.000,0.000,0.000]").arg(g));
        sendLine(QStringLiteral("[G28:0.000,0.000,0.000]"));
        sendLine(QStringLiteral("[G30:0.000,0.000,0.000]"));
        sendLine(QStringLiteral("[G92:%1]").arg(vec(m_g92)));
        sendLine(QStringLiteral("[TLO:0.000]"));
        sendLine(QStringLiteral("[PRB:0.000,0.000,0.000:0]"));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$G")) {
        const QString spindle = m_spindle == Spindle::M3 ? QStringLiteral("M3")
            : m_spindle == Spindle::M4                   ? QStringLiteral("M4")
                                                         : QStringLiteral("M5");
        sendLine(QStringLiteral("[GC:G%1 G54 G17 %2 %3 G94 %4 M%5 T0 F%6 S%7]")
                     .arg(m_motionMode)
                     .arg(m_inches ? QStringLiteral("G20") : QStringLiteral("G21"))
                     .arg(m_absolute ? QStringLiteral("G90") : QStringLiteral("G91"))
                     .arg(spindle)
                     .arg(m_coolant)
                     .arg(m_feed)
                     .arg(m_spindleS));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$I")) {
        sendLine(QStringLiteral("[VER:1.1h.20190825:]"));
        sendLine(QStringLiteral("[OPT:V,15,128]"));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$N")) {
        sendLine(QStringLiteral("$N0="));
        sendLine(QStringLiteral("$N1="));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$X")) {
        if (m_state == MachineState::Alarm) {
            m_state = MachineState::Idle;
            sendLine(QStringLiteral("[MSG:Caution: Unlocked]"));
        }
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$H")) {
        if (m_settings.number(22) < 0.5)
            return QStringLiteral("error:5");
        if (cycleActive)
            return QStringLiteral("error:8");
        m_state = MachineState::Home;
        m_homingPending = true;
        Block b;
        b.kind = Block::Home;
        m_planner.push_back(b);
        return {}; // "ok" is sent when homing has finished
    }
    if (line == QLatin1String("$C")) {
        if (cycleActive)
            return QStringLiteral("error:8");
        m_checkMode = !m_checkMode;
        m_state = m_checkMode ? MachineState::Check : MachineState::Idle;
        sendLine(m_checkMode ? QStringLiteral("[MSG:Enabled]") : QStringLiteral("[MSG:Disabled]"));
        return QStringLiteral("ok");
    }
    if (line == QLatin1String("$SLP")) {
        m_state = MachineState::Sleep;
        sendLine(QStringLiteral("[MSG:Sleeping]"));
        return QStringLiteral("ok");
    }
    if (line.startsWith(QLatin1String("$RST="))) {
        if (cycleActive)
            return QStringLiteral("error:8");
        const QString what = line.mid(5);
        if (what == QLatin1String("$") || what == QLatin1String("*"))
            loadDefaultSettings();
        if (what == QLatin1String("#") || what == QLatin1String("*")) {
            m_wco = Vec3 {};
            m_wcoChanged = true;
        }
        if (what != QLatin1String("$") && what != QLatin1String("#") && what != QLatin1String("*"))
            return QStringLiteral("error:3");
        return QStringLiteral("ok");
    }
    if (line.startsWith(QLatin1String("$J="))) {
        if (m_state != MachineState::Idle && m_state != MachineState::Jog)
            return QStringLiteral("error:8");
        return executeGcode(line.mid(3), true);
    }
    // $N=value
    int number = 0;
    QString value;
    if (GrblSettings::parseLine(line, &number, &value)) {
        if (!m_settings.has(number))
            return QStringLiteral("error:3");
        if (cycleActive)
            return QStringLiteral("error:8");
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok)
            return QStringLiteral("error:2");
        if (v < 0)
            return QStringLiteral("error:4");
        if (number == 0 && v < 3)
            return QStringLiteral("error:6");
        if (number == 20 && v > 0.5 && m_settings.number(22) < 0.5)
            return QStringLiteral("error:10");
        static const QList<int> integerSettings = { 0, 1, 2, 3, 4, 5, 6, 10, 13, 20, 21, 22, 23, 26, 30, 31, 32 };
        m_settings.set(number, integerSettings.contains(number) ? QString::number(qRound(v)) : fmt3(v));
        return QStringLiteral("ok");
    }
    return QStringLiteral("error:3");
}

QString SimulatedGrbl::executeGcode(const QString& line, bool jog)
{
    if (!jog) {
        ++m_gcodeLineCount;
        if (m_injectedErrors.contains(m_gcodeLineCount))
            return QStringLiteral("error:%1").arg(m_injectedErrors.value(m_gcodeLineCount));
    }

    // --- tokenize ---------------------------------------------------------
    struct Word
    {
        QChar letter;
        double value;
    };
    QList<Word> words;
    for (int i = 0; i < line.size();) {
        const QChar letter = line[i];
        if (!letter.isLetter())
            return QStringLiteral("error:1");
        ++i;
        const int start = i;
        if (i < line.size() && (line[i] == QLatin1Char('-') || line[i] == QLatin1Char('+')))
            ++i;
        while (i < line.size() && (line[i].isDigit() || line[i] == QLatin1Char('.')))
            ++i;
        bool ok = false;
        const double v = line.mid(start, i - start).toDouble(&ok);
        if (!ok)
            return QStringLiteral("error:2");
        words.append({ letter, v });
    }

    // --- classify ---------------------------------------------------------
    static const QString allowed = QStringLiteral("GMXYZFSPLNIJKRT");
    int motion = -1;     // 0..3
    int nonModal = -1;   // 4, 10, 28, 30, 53, 92 (x10 for decimals: 921 = G92.1)
    int distance = -1, units = -1;
    bool g53 = false;
    int spindleM = -1, coolantM = -1, programM = -1;
    std::optional<double> x, y, z, f, s, p, l;
    bool hasArcOffsets = false;
    QString seen;
    for (const Word& w : words) {
        if (!allowed.contains(w.letter))
            return QStringLiteral("error:20");
        const QChar c = w.letter;
        if (c != QLatin1Char('G') && c != QLatin1Char('M')) {
            if (seen.contains(c))
                return QStringLiteral("error:25");
            seen += c;
        }
        const int code10 = static_cast<int>(std::lround(w.value * 10));
        if (c == QLatin1Char('G')) {
            switch (code10) {
            case 0: case 10: case 20: case 30:
                if (motion >= 0)
                    return QStringLiteral("error:21");
                motion = code10 / 10;
                break;
            case 800:
                if (motion >= 0)
                    return QStringLiteral("error:21");
                motion = 80;
                break;
            case 40: case 100: case 280: case 300: case 920: case 921: case 281: case 301:
                if (nonModal >= 0)
                    return QStringLiteral("error:21");
                nonModal = code10;
                break;
            case 530:
                g53 = true;
                break;
            case 900: case 910:
                if (distance >= 0)
                    return QStringLiteral("error:21");
                distance = code10;
                break;
            case 200: case 210:
                if (units >= 0)
                    return QStringLiteral("error:21");
                units = code10;
                break;
            case 170: case 180: case 190: case 400: case 490: case 540: case 550: case 560:
            case 570: case 580: case 590: case 911: case 930: case 940:
                break; // accepted, no effect in the simulator
            default:
                return QStringLiteral("error:20");
            }
            if (jog && code10 != 200 && code10 != 210 && code10 != 900 && code10 != 910 && code10 != 530)
                return QStringLiteral("error:16");
        } else if (c == QLatin1Char('M')) {
            if (jog)
                return QStringLiteral("error:16");
            switch (code10) {
            case 30: case 40: case 50:
                if (spindleM >= 0)
                    return QStringLiteral("error:21");
                spindleM = code10 / 10;
                break;
            case 70: case 80: case 90:
                coolantM = code10 / 10;
                break;
            case 0: case 10: case 20: case 300:
                programM = code10 / 10;
                break;
            default:
                return QStringLiteral("error:20");
            }
        } else if (c == QLatin1Char('X')) {
            x = w.value;
        } else if (c == QLatin1Char('Y')) {
            y = w.value;
        } else if (c == QLatin1Char('Z')) {
            z = w.value;
        } else if (c == QLatin1Char('F')) {
            f = w.value;
        } else if (c == QLatin1Char('S')) {
            s = w.value;
        } else if (c == QLatin1Char('P')) {
            p = w.value;
        } else if (c == QLatin1Char('L')) {
            l = w.value;
        } else if (c == QLatin1Char('I') || c == QLatin1Char('J') || c == QLatin1Char('K') || c == QLatin1Char('R')) {
            hasArcOffsets = true;
        }
    }
    if (jog && !f)
        return QStringLiteral("error:22");
    if (f && *f < 0)
        return QStringLiteral("error:4");
    if (s && *s < 0)
        return QStringLiteral("error:4");

    const bool axisWords = x || y || z;
    const bool inches = units >= 0 ? units == 200 : m_inches;
    const bool absolute = distance >= 0 ? distance == 900 : m_absolute;
    const double scale = inches ? 25.4 : 1.0;

    // Validate before changing any state.
    int newMotion = motion >= 0 ? motion : m_motionMode;
    if (jog)
        newMotion = 1;
    const bool axisConsumedByNonModal = nonModal == 100 || nonModal == 280 || nonModal == 300 || nonModal == 920;
    const bool needsMove = axisWords && !axisConsumedByNonModal && newMotion != 80;
    const double newFeed = f ? *f * scale : m_feed;
    if (needsMove && newMotion >= 1 && newMotion <= 3 && newFeed <= 0)
        return QStringLiteral("error:22");
    if ((motion == 2 || motion == 3) && !hasArcOffsets)
        return QStringLiteral("error:26");
    if (nonModal == 100 && (!l || !p))
        return QStringLiteral("error:28");
    if (axisWords && newMotion == 80 && !axisConsumedByNonModal)
        return QStringLiteral("error:31");

    auto targetFrom = [&](const Vec3& base) {
        Vec3 t = base;
        auto apply = [&](const std::optional<double>& w, double& out, double off) {
            if (!w)
                return;
            const double v = *w * scale;
            if (g53)
                out = v;
            else if (absolute)
                out = v + off;
            else
                out = out + v;
        };
        apply(x, t.x, m_wco.x + m_g92.x);
        apply(y, t.y, m_wco.y + m_g92.y);
        apply(z, t.z, m_wco.z + m_g92.z);
        return t;
    };

    if (jog) {
        Vec3 t = m_plannedPos;
        // Jog defaults to the current distance mode unless G90/G91 is given.
        const bool jogAbs = distance >= 0 ? distance == 900 : m_absolute;
        auto apply = [&](const std::optional<double>& w, double& out, double off) {
            if (!w)
                return;
            const double v = *w * scale;
            out = g53 ? v : (jogAbs ? v + off : out + v);
        };
        apply(x, t.x, m_wco.x + m_g92.x);
        apply(y, t.y, m_wco.y + m_g92.y);
        apply(z, t.z, m_wco.z + m_g92.z);
        if (m_settings.number(20) > 0.5) {
            const double tx = m_settings.number(130), ty = m_settings.number(131);
            if (t.x < -0.0001 || t.y < -0.0001 || t.x > tx + 0.0001 || t.y > ty + 0.0001)
                return QStringLiteral("error:15");
        }
        Block b;
        b.target = t;
        b.feed = *f * scale;
        b.rapid = false;
        b.jog = true;
        b.spindle = Spindle::Off;
        m_planner.push_back(b);
        m_plannedPos = t;
        if (m_state == MachineState::Idle)
            m_state = MachineState::Jog;
        return QStringLiteral("ok");
    }

    // --- apply modal state -----------------------------------------------
    if (units >= 0)
        m_inches = units == 200;
    if (distance >= 0)
        m_absolute = distance == 900;
    if (f)
        m_feed = newFeed;
    if (s)
        m_spindleS = std::min(*s, m_settings.number(30, 1000));
    if (spindleM == 3)
        m_spindle = Spindle::M3;
    else if (spindleM == 4)
        m_spindle = Spindle::M4;
    else if (spindleM == 5)
        m_spindle = Spindle::Off;
    if (coolantM >= 0)
        m_coolant = coolantM;
    if (motion >= 0)
        m_motionMode = motion;

    if (m_checkMode)
        return QStringLiteral("ok");

    if (nonModal == 40) {
        Block b;
        b.kind = Block::Dwell;
        b.dwellSeconds = p.value_or(0);
        b.spindle = m_spindle;
        b.s = m_spindleS;
        m_planner.push_back(b);
    } else if (nonModal == 100) {
        const int lv = static_cast<int>(*l);
        Vec3 given = { x.value_or(0) * scale, y.value_or(0) * scale, z.value_or(0) * scale };
        if (lv == 2) {
            if (x) m_wco.x = given.x;
            if (y) m_wco.y = given.y;
            if (z) m_wco.z = given.z;
        } else if (lv == 20) {
            if (x) m_wco.x = m_plannedPos.x - m_g92.x - given.x;
            if (y) m_wco.y = m_plannedPos.y - m_g92.y - given.y;
            if (z) m_wco.z = m_plannedPos.z - m_g92.z - given.z;
        } else {
            return QStringLiteral("error:20");
        }
        m_wcoChanged = true;
    } else if (nonModal == 920) {
        if (x) m_g92.x = m_plannedPos.x - m_wco.x - *x * scale;
        if (y) m_g92.y = m_plannedPos.y - m_wco.y - *y * scale;
        if (z) m_g92.z = m_plannedPos.z - m_wco.z - *z * scale;
        m_wcoChanged = true;
    } else if (nonModal == 921) {
        m_g92 = Vec3 {};
        m_wcoChanged = true;
    } else if (nonModal == 280 || nonModal == 300) {
        if (axisWords) {
            Block via;
            via.target = targetFrom(m_plannedPos);
            via.rapid = true;
            via.spindle = m_spindle;
            via.s = m_spindleS;
            m_planner.push_back(via);
            m_plannedPos = via.target;
        }
        Block home;
        home.target = Vec3 {};
        home.rapid = true;
        home.spindle = m_spindle;
        home.s = m_spindleS;
        m_planner.push_back(home);
        m_plannedPos = home.target;
    } else if (needsMove) {
        Block b;
        b.target = targetFrom(m_plannedPos);
        b.rapid = newMotion == 0;
        b.feed = b.rapid ? 0 : m_feed;
        b.spindle = m_spindle;
        b.s = m_spindleS;
        m_planner.push_back(b);
        m_plannedPos = b.target;
    }

    if (programM == 0 || programM == 1) {
        if (!m_planner.empty() || m_state == MachineState::Run) {
            m_feedHold = true;
            m_state = MachineState::Hold;
            m_holdSub = 1;
        }
    } else if (programM == 2 || programM == 30) {
        m_absolute = true;
        m_inches = false;
        m_motionMode = 1;
        m_spindle = Spindle::Off;
        m_coolant = 9;
    }
    if (!m_planner.empty() && m_state == MachineState::Idle)
        m_state = MachineState::Run;
    return QStringLiteral("ok");
}

void SimulatedGrbl::advanceMotion(double dt)
{
    const double accel = std::max(1.0, std::min(m_settings.number(120, 500), m_settings.number(121, 500))) * 3600.0;
    const double maxRate = std::max(1.0, std::min(m_settings.number(110, 6000), m_settings.number(111, 6000)));
    double remainingTime = dt / 60.0; // minutes
    for (int guard = 0; guard < 10000 && remainingTime > 0; ++guard) {
        if (m_planner.empty()) {
            m_velocity = 0;
            if (m_state == MachineState::Run || m_state == MachineState::Jog)
                m_state = MachineState::Idle;
            if (m_state == MachineState::Hold)
                m_holdSub = 0;
            return;
        }
        Block& b = m_planner.front();
        if (b.kind == Block::Home) {
            // Homing takes about one second of simulated time.
            m_dwellLeft = m_dwellLeft > 0 ? m_dwellLeft : 1.0 / 60.0;
            const double used = std::min(m_dwellLeft, remainingTime);
            m_dwellLeft -= used;
            remainingTime -= used;
            if (m_dwellLeft <= 1e-12) {
                m_dwellLeft = 0;
                m_planner.pop_front();
                m_mpos = m_plannedPos = Vec3 {};
                m_homingPending = false;
                m_state = MachineState::Idle;
                sendLine(QStringLiteral("ok"));
            }
            continue;
        }
        if (b.kind == Block::Dwell) {
            if (m_feedHold)
                return;
            if (m_dwellLeft <= 0)
                m_dwellLeft = b.dwellSeconds / 60.0 + 1e-9;
            const double used = std::min(m_dwellLeft, remainingTime);
            m_dwellLeft -= used;
            remainingTime -= used;
            if (m_dwellLeft <= 1e-9) {
                m_dwellLeft = 0;
                m_planner.pop_front();
            }
            continue;
        }

        if (!m_feedHold)
            m_state = b.jog ? MachineState::Jog : MachineState::Run;

        const double dx = b.target.x - m_mpos.x, dy = b.target.y - m_mpos.y, dz = b.target.z - m_mpos.z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 1e-9) {
            m_mpos = b.target;
            m_planner.pop_front();
            continue;
        }

        if (m_feedHold) {
            m_velocity = std::max(0.0, m_velocity - accel * remainingTime);
            m_holdSub = m_velocity > 0 ? 1 : 0;
            if (m_velocity <= 0)
                return;
        } else {
            double vmax = b.rapid ? maxRate * m_ovRapid / 100.0 : b.feed * (b.jog ? 1.0 : m_ovFeed / 100.0);
            vmax = std::min(vmax, maxRate);
            const double vBrake = std::sqrt(2.0 * accel * dist);
            m_velocity = std::min({ vmax, m_velocity + accel * remainingTime, vBrake });
            m_velocity = std::max(m_velocity, std::min(vmax, 30.0)); // avoid stalling
        }

        const double step = m_velocity * remainingTime;
        if (step >= dist) {
            remainingTime -= dist / m_velocity;
            m_mpos = b.target;
            m_planner.pop_front();
        } else {
            const double k = step / dist;
            m_mpos.x += dx * k;
            m_mpos.y += dy * k;
            m_mpos.z += dz * k;
            remainingTime = 0;
        }
    }
}

void SimulatedGrbl::sendStatus()
{
    QString st;
    switch (m_state) {
    case MachineState::Hold: st = QStringLiteral("Hold:%1").arg(m_holdSub); break;
    case MachineState::Run: st = QStringLiteral("Run"); break;
    case MachineState::Jog: st = QStringLiteral("Jog"); break;
    case MachineState::Alarm: st = QStringLiteral("Alarm"); break;
    case MachineState::Home: st = QStringLiteral("Home"); break;
    case MachineState::Check: st = QStringLiteral("Check"); break;
    case MachineState::Sleep: st = QStringLiteral("Sleep"); break;
    case MachineState::Door: st = QStringLiteral("Door:0"); break;
    default: st = QStringLiteral("Idle"); break;
    }
    const int mask = static_cast<int>(m_settings.number(10, 1));
    const Vec3 offset = { m_wco.x + m_g92.x, m_wco.y + m_g92.y, m_wco.z + m_g92.z };
    QString r = QLatin1Char('<') + st;
    if (mask & 1)
        r += QStringLiteral("|MPos:") + vec(m_mpos);
    else
        r += QStringLiteral("|WPos:") + vec({ m_mpos.x - offset.x, m_mpos.y - offset.y, m_mpos.z - offset.z });
    if (mask & 2)
        r += QStringLiteral("|Bf:%1,%2").arg(static_cast<int>(kPlannerSize - m_planner.size())).arg(kRxBufferSize - 1 - m_rx.size());
    const double s = m_spindle == Spindle::Off ? 0.0 : m_spindleS;
    r += QStringLiteral("|FS:%1,%2").arg(qRound(m_velocity)).arg(qRound(s));
    const int n = m_statusCount++;
    if (m_wcoChanged || n % 10 == 0) {
        r += QStringLiteral("|WCO:") + vec(offset);
        m_wcoChanged = false;
    } else if (m_ovChanged || n % 10 == 5) {
        r += QStringLiteral("|Ov:%1,%2,%3").arg(m_ovFeed).arg(m_ovRapid).arg(m_ovSpindle);
        m_ovChanged = false;
    }
    r += QLatin1Char('>');
    sendLine(r);
}
