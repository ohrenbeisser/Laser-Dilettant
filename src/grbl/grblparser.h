// Auswertung der Antworten von GRBL 1.1: Statuszeilen, Fehler- und Alarmtexte (siehe planung.md 3.15).
#pragma once

#include <QPointF>
#include <QString>
#include <optional>

namespace ld::grbl {

enum class MachineState { Unknown, Idle, Run, Hold, Jog, Alarm, Door, Check, Home, Sleep };

struct GrblStatus {
    MachineState state = MachineState::Unknown;
    int subState = -1;                 // zum Beispiel Hold:0 / Hold:1
    QPointF mpos;                      // Maschinenposition in mm (X, Y)
    QPointF wpos;                      // Arbeitsposition (Nullpunkt = X0 Y0), falls berechenbar
    bool hasPosition = false;
    int feed = 0;                      // aktueller Vorschub in mm/min
    int spindle = 0;                   // aktueller S-Wert
    int plannerFree = -1;              // freie Blöcke im Planer (Bf:)
    int rxFree = -1;                   // freie Bytes im Empfangspuffer (Bf:)
    int ovFeed = -1;                   // Override Vorschub in % (Ov:), -1 = nicht gemeldet
    int ovRapid = -1;
    int ovSpindle = -1;                // Override Leistung in %
};

// Wertet eine Statuszeile wie "<Idle|MPos:1.000,2.000,0.000|FS:0,0>" aus. wco ist der gemerkte
// Offset zwischen Maschinen- und Arbeitskoordinaten; er wird aktualisiert, wenn die Zeile "WCO:"
// enthält. Gibt es nur WPos (Einstellung $10), wird mpos daraus berechnet.
// Liefert nullopt, wenn die Zeile keine Statuszeile ist.
std::optional<GrblStatus> parseStatusLine(const QString& line, QPointF& wco);

QString stateName(MachineState state);

// Begrenzt jedes S-Wort einer G-Code-Zeile auf maxS (zum Beispiel $30, das größte S des Lasers), damit nie mehr
// Leistung angefordert wird, als GRBL kennt. Kommentare in Klammern und nach ';' sowie Einstellungszeilen ($...)
// bleiben unberührt. maxS <= 0: unbekannt, nichts wird geändert. requested (falls gegeben) bekommt den größten
// angeforderten Wert, der begrenzt wurde.
QString limitSpindleWords(const QString& line, double maxS, double* requested = nullptr);

// Deutsche Beschreibung eines GRBL-Fehlers (error:n) bzw. Alarms (ALARM:n).
QString errorText(int code);
QString alarmText(int code);

}  // namespace ld::grbl
