// Gravur-Einstellungen: Was der Laser tut (Operation) und wo der Auftrag am Nullpunkt sitzt.
// Siehe docs/planung.md 3.10.
#pragma once

#include <QPointF>
#include <QSizeF>

namespace ld::core {

enum class EngraveMode {
    Line,     // Rasterzeilen (M3)
    Full,     // zusammenhängende Flächen im Zickzack füllen (M6)
    Cutting,  // Konturen (M6)
};

// Speed, Leistung und Durchgänge: Diese drei Werte unterscheiden sich stark zwischen Gravieren
// und Schneiden und gehören deshalb je Gravurart-Gruppe zusammen (siehe planung.md 3.14).
struct ModeValues {
    double speedMmPerMin = 1200.0;
    double powerPercent = 60.0;
    int passes = 1;

    friend bool operator==(const ModeValues&, const ModeValues&) = default;
};

// Vorgabewerte für Schneiden (Cutting): langsam und mit voller Leistung.
inline ModeValues defaultCutValues() { return ModeValues{300.0, 100.0, 1}; }

// Ein Arbeitsschritt. Intern ist "Operation" eine Liste mit einem Eintrag, damit das spätere
// Ebenenkonzept erweiterbar bleibt (siehe planung.md 3.5).
struct Operation {
    EngraveMode mode = EngraveMode::Line;
    double angleDeg = 0.0;          // Richtung der Zeilen, 0..180, gegen den Uhrzeigersinn, Sicht des Bedieners
    double speedMmPerMin = 1200.0;  // Vorschub beim Gravieren
    double powerPercent = 60.0;     // 0..100 % von S max
    int passes = 1;                 // Durchgänge
    double overscanMm = 1.0;        // Anlauf/Auslauf ohne Leistung an beiden Zeilenenden
    bool bidirectional = true;      // Hin- und Rückweg gravieren
    // Größte Abweichung beim Zerlegen von Kurven in Strecken (Full und Cutting), in mm.
    double toleranceMm = 0.02;
    // Die Werte der jeweils anderen Gruppe: Gravur (Linie, Full) oder Schnitt (Cutting).
    // speedMmPerMin, powerPercent und passes gelten für die aktuelle Gruppe.
    ModeValues otherValues = defaultCutValues();

    friend bool operator==(const Operation&, const Operation&) = default;
};

// Gehört die Gravurart zur Gruppe "Schneiden"? (Cutting; Linie und Full gravieren.)
inline bool isCutMode(EngraveMode mode) { return mode == EngraveMode::Cutting; }

// Wechselt die Gravurart. Wechselt dabei die Gruppe (Gravur <-> Schnitt), tauschen die Werte für
// Speed, Leistung und Durchgänge mit denen der anderen Gruppe (otherValues).
Operation switchMode(const Operation& operation, EngraveMode mode);

// Die beiden Wertesätze unabhängig davon, welcher gerade aktiv ist.
ModeValues engraveValuesOf(const Operation& operation);
ModeValues cutValuesOf(const Operation& operation);
// Setzt beide Wertesätze; der zur aktuellen Gravurart passende wird zum aktiven.
Operation withModeValues(const Operation& operation, const ModeValues& engrave, const ModeValues& cut);


// Welcher Punkt des Auftrags (umschließendes Rechteck) auf den Nullpunkt gelegt wird.
enum class JobOrigin {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
};

// Lage des Ursprungs im Rechteck als Anteil (0, 0.5 oder 1) von Breite und Höhe;
// x nach rechts, y nach unten (Sicht des Bedieners).
QPointF jobOriginFraction(JobOrigin origin);

// Zur Wahl stehen fünf Ursprünge (wie die Fünf auf dem Würfel): die vier Ecken und die Mitte (planung.md 3.23).
// Die Kantenmitten gibt es nur noch als alte Werte in Projekten und Einstellungen; sie gelten als Mitte.
JobOrigin normalizedJobOrigin(JobOrigin origin);
inline bool isSelectableJobOrigin(JobOrigin origin) { return normalizedJobOrigin(origin) == origin; }

// Zählrichtung der Lineale und der Bemaßung, vom Ursprung aus (+1 oder -1 je Achse), in der Sicht des Bedieners
// (x nach rechts, y nach unten). Jede Achse zählt vom Ursprung weg ins Bild hinein positiv; bei der Mitte zählt X
// nach rechts und Y nach oben positiv (wie im Koordinatensystem und beim Jog).
QPointF jobOriginRulerSign(JobOrigin origin);

}  // namespace ld::core
