#include "grbl/grblparser.h"

#include <QCoreApplication>
#include <QStringList>

namespace ld::grbl {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::grbl", text);
}

MachineState stateFromText(const QString& text)
{
    if (text == QLatin1String("Idle")) return MachineState::Idle;
    if (text == QLatin1String("Run")) return MachineState::Run;
    if (text == QLatin1String("Hold")) return MachineState::Hold;
    if (text == QLatin1String("Jog")) return MachineState::Jog;
    if (text == QLatin1String("Alarm")) return MachineState::Alarm;
    if (text == QLatin1String("Door")) return MachineState::Door;
    if (text == QLatin1String("Check")) return MachineState::Check;
    if (text == QLatin1String("Home")) return MachineState::Home;
    if (text == QLatin1String("Sleep")) return MachineState::Sleep;
    return MachineState::Unknown;
}

bool parseXY(const QString& value, QPointF& out)
{
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() < 2) return false;
    bool okX = false, okY = false;
    const double x = parts[0].toDouble(&okX);
    const double y = parts[1].toDouble(&okY);
    if (!okX || !okY) return false;
    out = QPointF(x, y);
    return true;
}

}  // namespace

std::optional<GrblStatus> parseStatusLine(const QString& rawLine, QPointF& wco)
{
    const QString line = rawLine.trimmed();
    if (!line.startsWith(QLatin1Char('<')) || !line.endsWith(QLatin1Char('>'))) {
        return std::nullopt;
    }
    const QStringList fields = line.mid(1, line.size() - 2).split(QLatin1Char('|'));
    if (fields.isEmpty()) return std::nullopt;

    GrblStatus status;
    const QString stateField = fields[0];
    const int colon = stateField.indexOf(QLatin1Char(':'));
    status.state = stateFromText(colon < 0 ? stateField : stateField.left(colon));
    if (colon >= 0) status.subState = stateField.mid(colon + 1).toInt();

    QPointF mpos, wpos;
    bool haveM = false, haveW = false;
    for (int i = 1; i < fields.size(); ++i) {
        const QString& f = fields[i];
        const int c = f.indexOf(QLatin1Char(':'));
        if (c < 0) continue;
        const QString key = f.left(c), value = f.mid(c + 1);
        if (key == QLatin1String("MPos")) {
            haveM = parseXY(value, mpos);
        } else if (key == QLatin1String("WPos")) {
            haveW = parseXY(value, wpos);
        } else if (key == QLatin1String("WCO")) {
            parseXY(value, wco);
        } else if (key == QLatin1String("FS")) {
            const QStringList p = value.split(QLatin1Char(','));
            if (p.size() >= 2) {
                status.feed = p[0].toInt();
                status.spindle = p[1].toInt();
            }
        } else if (key == QLatin1String("F")) {
            status.feed = value.toInt();
        } else if (key == QLatin1String("Ov")) {
            const QStringList p = value.split(QLatin1Char(','));
            if (p.size() >= 3) {
                status.ovFeed = p[0].toInt();
                status.ovRapid = p[1].toInt();
                status.ovSpindle = p[2].toInt();
            }
        } else if (key == QLatin1String("Bf")) {
            const QStringList p = value.split(QLatin1Char(','));
            if (p.size() >= 2) {
                status.plannerFree = p[0].toInt();
                status.rxFree = p[1].toInt();
            }
        }
    }
    if (haveM) {
        status.mpos = mpos;
        status.wpos = mpos - wco;
        status.hasPosition = true;
    } else if (haveW) {
        status.wpos = wpos;
        status.mpos = wpos + wco;
        status.hasPosition = true;
    }
    return status;
}

QString stateName(MachineState state)
{
    switch (state) {
    case MachineState::Idle: return tr("Bereit");
    case MachineState::Run: return tr("Läuft");
    case MachineState::Hold: return tr("Pause");
    case MachineState::Jog: return tr("Jog");
    case MachineState::Alarm: return tr("Alarm");
    case MachineState::Door: return tr("Tür offen");
    case MachineState::Check: return tr("Prüfmodus");
    case MachineState::Home: return tr("Referenzfahrt");
    case MachineState::Sleep: return tr("Schlaf");
    case MachineState::Unknown: break;
    }
    return QStringLiteral("–");
}

QString errorText(int code)
{
    switch (code) {
    case 1: return tr("Ein G-Code-Wort besteht nicht aus Buchstabe und Zahl.");
    case 2: return tr("Zahlenformat ungültig oder Zahl fehlt.");
    case 3: return tr("Der $-Befehl ist GRBL nicht bekannt.");
    case 4: return tr("Negativer Wert, wo ein positiver erwartet wird.");
    case 5: return tr("Referenzfahrt ist in den Einstellungen nicht aktiv.");
    case 6: return tr("Mindest-Schrittimpuls muss größer als 3 µs sein.");
    case 7: return tr("EEPROM konnte nicht gelesen werden; Werte wurden zurückgesetzt.");
    case 8: return tr("Der $-Befehl geht nur im Zustand Bereit.");
    case 9: return tr("G-Code ist bei Alarm oder Jog gesperrt.");
    case 10: return tr("Soft-Limits gehen nur mit aktiver Referenzfahrt.");
    case 11: return tr("Zeile zu lang (mehr als 80 Zeichen).");
    case 12: return tr("Einstellung überschreitet die größte Schrittrate.");
    case 13: return tr("Sicherheitstür ist offen.");
    case 14: return tr("Start- oder Info-Zeile ist zu lang für den EEPROM.");
    case 15: return tr("Jog-Ziel liegt außerhalb des Fahrweges; Befehl ignoriert.");
    case 16: return tr("Jog-Befehl ohne '=' oder mit unzulässigem G-Code.");
    case 17: return tr("Laser-Modus braucht einen PWM-Ausgang.");
    case 20: return tr("Nicht unterstützter oder ungültiger G-Code-Befehl.");
    case 21: return tr("Mehr als ein Befehl derselben Gruppe in einer Zeile.");
    case 22: return tr("Vorschub (F) ist noch nicht gesetzt.");
    case 23: return tr("Der G-Code-Befehl braucht eine ganze Zahl.");
    case 24: return tr("Zwei Befehle, die beide Achsenwerte brauchen.");
    case 25: return tr("Ein G-Code-Wort kommt in der Zeile doppelt vor.");
    case 26: return tr("Der Befehl braucht Achsenwerte, es gibt keine.");
    case 27: return tr("Zeilennummer (N) außerhalb des Bereichs.");
    case 28: return tr("Dem Befehl fehlt ein P- oder L-Wert.");
    case 29: return tr("GRBL kennt nur die Koordinatensysteme G54 bis G59.");
    case 30: return tr("G53 braucht G0 oder G1.");
    case 31: return tr("Achsenwerte ohne passenden Befehl (G80 aktiv).");
    case 32: return tr("Kreisbogen ohne Achsenwerte in der Ebene.");
    case 33: return tr("Ungültiges Bewegungsziel.");
    case 34: return tr("Rechenfehler bei einem Kreisbogen mit Radius.");
    case 35: return tr("Kreisbogen ohne IJK-Versatz.");
    case 36: return tr("Übrig gebliebene, nicht benutzte G-Code-Wörter.");
    case 37: return tr("G43.1 kann nur die konfigurierte Achse verschieben.");
    case 38: return tr("Werkzeugnummer zu groß.");
    default: break;
    }
    return tr("Unbekannter Fehler %1.").arg(code);
}

QString alarmText(int code)
{
    switch (code) {
    case 1: return tr("Endschalter ausgelöst; die Position ist wahrscheinlich verloren.");
    case 2: return tr("Das Ziel liegt außerhalb des Fahrweges; die Position blieb erhalten.");
    case 3: return tr("Reset während der Fahrt; die Position ist nicht mehr sicher.");
    case 4: return tr("Tastfehler: Taster nicht im erwarteten Zustand.");
    case 5: return tr("Tastfehler: Werkstück wurde nicht berührt.");
    case 6: return tr("Referenzfahrt abgebrochen (Reset).");
    case 7: return tr("Referenzfahrt: Sicherheitstür wurde geöffnet.");
    case 8: return tr("Referenzfahrt: Endschalter ließ sich nicht freifahren.");
    case 9: return tr("Referenzfahrt: Kein Endschalter gefunden.");
    default: break;
    }
    return tr("Unbekannter Alarm %1.").arg(code);
}

QString limitSpindleWords(const QString& line, double maxS, double* requested)
{
    if (maxS <= 0.0 || line.startsWith(QLatin1Char('$'))) return line;
    QString result;
    result.reserve(line.size());
    int comment = 0;   // Verschachtelung von Klammerkommentaren
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (comment > 0) {
            if (c == QLatin1Char('(')) ++comment;
            if (c == QLatin1Char(')')) --comment;
            result += c;
            continue;
        }
        if (c == QLatin1Char(';')) {   // Rest der Zeile ist Kommentar
            result += line.mid(i);
            break;
        }
        if (c == QLatin1Char('(')) {
            comment = 1;
            result += c;
            continue;
        }
        if (c == QLatin1Char('S') || c == QLatin1Char('s')) {
            int j = i + 1;
            while (j < line.size() && (line.at(j).isDigit() || line.at(j) == QLatin1Char('.'))) ++j;
            bool ok = false;
            const double value = line.mid(i + 1, j - i - 1).toDouble(&ok);
            if (ok && j > i + 1) {
                if (value > maxS) {
                    if (requested && value > *requested) *requested = value;
                    result += c;
                    result += QString::number(static_cast<long long>(maxS));
                } else {
                    result += line.mid(i, j - i);
                }
                i = j - 1;
                continue;
            }
        }
        result += c;
    }
    return result;
}

}  // namespace ld::grbl
