#include "device/GrblStatusParser.h"

#include <QStringList>

namespace {

bool parseVec(const QString& s, Vec3* v)
{
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() < 2)
        return false;
    bool okX = false, okY = false, okZ = true;
    v->x = parts[0].toDouble(&okX);
    v->y = parts[1].toDouble(&okY);
    v->z = parts.size() > 2 ? parts[2].toDouble(&okZ) : 0.0;
    return okX && okY && okZ;
}

} // namespace

QString machineStateName(MachineState s)
{
    switch (s) {
    case MachineState::Disconnected: return QStringLiteral("Disconnected");
    case MachineState::Connecting: return QStringLiteral("Connecting");
    case MachineState::Unknown: return QStringLiteral("Unknown");
    case MachineState::Idle: return QStringLiteral("Idle");
    case MachineState::Run: return QStringLiteral("Run");
    case MachineState::Hold: return QStringLiteral("Hold");
    case MachineState::Jog: return QStringLiteral("Jog");
    case MachineState::Alarm: return QStringLiteral("Alarm");
    case MachineState::Door: return QStringLiteral("Door");
    case MachineState::Check: return QStringLiteral("Check");
    case MachineState::Home: return QStringLiteral("Home");
    case MachineState::Sleep: return QStringLiteral("Sleep");
    }
    return QStringLiteral("Unknown");
}

namespace GrblStatusParser {

MachineState stateFromString(const QString& word)
{
    static const struct { const char* name; MachineState state; } table[] = {
        { "Idle", MachineState::Idle },   { "Run", MachineState::Run },
        { "Hold", MachineState::Hold },   { "Jog", MachineState::Jog },
        { "Alarm", MachineState::Alarm }, { "Door", MachineState::Door },
        { "Check", MachineState::Check }, { "Home", MachineState::Home },
        { "Sleep", MachineState::Sleep },
    };
    for (const auto& e : table)
        if (word.compare(QLatin1String(e.name), Qt::CaseInsensitive) == 0)
            return e.state;
    return MachineState::Unknown;
}

bool parse(const QString& rawLine, StatusReport* out)
{
    const QString line = rawLine.trimmed();
    if (!line.startsWith(QLatin1Char('<')) || !line.endsWith(QLatin1Char('>')))
        return false;
    const QString body = line.mid(1, line.size() - 2);
    // GRBL 0.9 used ',' between fields; 1.1 uses '|'. Only 1.1 is supported,
    // but we still try to read the state from older reports.
    const QStringList fields = body.split(QLatin1Char('|'));
    if (fields.isEmpty() || fields[0].isEmpty())
        return false;

    StatusReport r;
    QString stateField = fields[0];
    if (!body.contains(QLatin1Char('|')))
        stateField = stateField.section(QLatin1Char(','), 0, 0);
    r.stateText = stateField;
    const int colon = stateField.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        bool ok = false;
        const int sub = stateField.mid(colon + 1).toInt(&ok);
        if (ok)
            r.subState = sub;
        stateField = stateField.left(colon);
    }
    r.state = stateFromString(stateField);

    for (int i = 1; i < fields.size(); ++i) {
        const QString& f = fields[i];
        const int c = f.indexOf(QLatin1Char(':'));
        if (c < 0)
            continue;
        const QString key = f.left(c);
        const QString val = f.mid(c + 1);
        Vec3 v;
        if (key == QLatin1String("MPos")) {
            if (parseVec(val, &v))
                r.mpos = v;
        } else if (key == QLatin1String("WPos")) {
            if (parseVec(val, &v))
                r.wpos = v;
        } else if (key == QLatin1String("WCO")) {
            if (parseVec(val, &v))
                r.wco = v;
        } else if (key == QLatin1String("FS") || key == QLatin1String("F")) {
            const QStringList p = val.split(QLatin1Char(','));
            bool ok = false;
            if (!p.isEmpty()) {
                const double fv = p[0].toDouble(&ok);
                if (ok)
                    r.feed = fv;
            }
            if (p.size() > 1) {
                const double sv = p[1].toDouble(&ok);
                if (ok)
                    r.spindle = sv;
            }
        } else if (key == QLatin1String("Ov")) {
            const QStringList p = val.split(QLatin1Char(','));
            if (p.size() >= 3) {
                bool a = false, b = false, d = false;
                const int f1 = p[0].toInt(&a), f2 = p[1].toInt(&b), f3 = p[2].toInt(&d);
                if (a && b && d) {
                    r.ovFeed = f1;
                    r.ovRapid = f2;
                    r.ovSpindle = f3;
                }
            }
        } else if (key == QLatin1String("Bf")) {
            const QStringList p = val.split(QLatin1Char(','));
            if (p.size() >= 2) {
                r.plannerFree = p[0].toInt();
                r.rxFree = p[1].toInt();
            }
        } else if (key == QLatin1String("Ln")) {
            r.lineNumber = val.toInt();
        } else if (key == QLatin1String("Pn")) {
            r.pins = val;
        } else if (key == QLatin1String("A")) {
            r.accessories = val;
        }
        // Anything else (manufacturer extensions) is ignored.
    }
    *out = r;
    return true;
}

void merge(const StatusReport& r, MachineStatus* s)
{
    s->state = r.state;
    s->subState = r.subState;
    if (r.wco)
        s->wco = *r.wco;
    if (r.mpos) {
        s->mpos = *r.mpos;
        s->wpos = { r.mpos->x - s->wco.x, r.mpos->y - s->wco.y, r.mpos->z - s->wco.z };
    } else if (r.wpos) {
        s->wpos = *r.wpos;
        s->mpos = { r.wpos->x + s->wco.x, r.wpos->y + s->wco.y, r.wpos->z + s->wco.z };
    }
    if (r.feed)
        s->feed = *r.feed;
    if (r.spindle)
        s->spindle = *r.spindle;
    if (r.ovFeed) {
        s->ovFeed = *r.ovFeed;
        s->ovRapid = *r.ovRapid;
        s->ovSpindle = *r.ovSpindle;
    }
    // Pn: is only sent when a pin is active, so absence means "none".
    s->pins = r.pins;
}

} // namespace GrblStatusParser
