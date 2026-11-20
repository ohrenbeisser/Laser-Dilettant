// Maschinenprofil (siehe docs/planung.md 3.12): alles, was die G-Code-Erzeugung, die Prüfung des
// Arbeitsbereichs und die Anzeige über den Laser wissen müssen. Wird als JSON gespeichert, eine
// Datei pro Profil.
#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <optional>

namespace ld::core {

enum class LaserCommand {
    M3,  // konstante Leistung (GRBL ohne Laser-Modus)
    M4,  // dynamische Leistung (GRBL Laser-Modus, $32=1)
};

struct MachineSetup {
    QString name = QStringLiteral("DIY8000-Desktop1720");

    // Arbeitsbereich in den Achsen der Maschine (mm). Vorgabe: X = 500, Y = 400.
    double workAreaX = 500.0;
    double workAreaY = 400.0;

    // Aufstellung des Lasers relativ zum Bediener (siehe planung.md 2 und 3.10):
    // Standard 0° ohne Spiegeln heißt: +X zeigt nach rechts, +Y vom Bediener weg.
    // rotationDeg (0/90/180/270) dreht den ganzen Laser im Uhrzeigersinn (von oben gesehen);
    // mirrorX bedeutet, dass die X-Achse der Maschine umgekehrt zeigt.
    int rotationDeg = 0;
    bool mirrorX = false;

    LaserCommand laserCommand = LaserCommand::M4;
    int maxPower = 1000;              // S-Wert für 100 % ($30). Die App sendet nie mehr.
    double rapidMmPerMin = 3000.0;    // Eilgang, für die Zeitschätzung
    double maxSpeedMmPerMin = 3000.0; // höchste Gravur-Speed: obere Grenze des Speed-Reglers (planung.md 3.24)
    // Beschleunigung der Achsen in mm/s² (GRBL $120/$121). 0 = unbekannt: Die Zeitschätzung
    // rechnet dann ohne Anfahren und Bremsen.
    double accelerationX = 0.0;
    double accelerationY = 0.0;

    QString startCode = QStringLiteral("G21 G90 G94");
    QString endCode = QStringLiteral("M5");
    // Vor dem Start zum Nullpunkt fahren ("G0 X0 Y0" nach dem Einschalten des Lasers mit S0). Ohne Haken geht der Auftrag
    // davon aus, dass der Laser schon auf dem Nullpunkt steht.
    bool moveToZeroBeforeStart = false;
    // Nach dem Auftrag zum Nullpunkt zurückfahren ("G0 X0 Y0" nach dem Ende-Code). Ohne Haken bleibt der Laser dort stehen,
    // wo der Auftrag endet.
    bool returnToZeroAfterJob = true;
    // Jog (Fahren von Hand) mit dem GRBL-Jog-Befehl "$J=". Nicht angehakt (Vorgabe): Das Jog fährt mit normalem G-Code
    // ("G91", "G0 ..." und "G90"). Manche GRBL-Varianten (zum Beispiel "1.1z" am DIY8000) quittieren "$J=" mit "ok",
    // fahren aber nicht; der G-Code-Weg geht überall.
    bool jogWithJogCommand = false;

    int baudRate = 115200;            // serielle Geschwindigkeit zum Laser (GRBL-Standard 115200)
    double laserDpi = 400.0;          // Auflösung des Lasers, ergibt die Vorgabe-Pixelgröße
    // Korrekturfaktoren aus der Kalibrierung (planung.md 3.25): Die Koordinaten der Maschinenachsen werden beim Erzeugen
    // des G-Codes damit multipliziert (Faktor = Soll / gemessen). 1 = keine Korrektur.
    double correctionX = 1.0;
    double correctionY = 1.0;
    bool hasZAxis = false;
    bool hasEndstops = false;         // nur gespeichert, die Wirkung kommt später

    friend bool operator==(const MachineSetup&, const MachineSetup&) = default;
};

// Punkt aus Sicht des Bedieners (mm, X nach rechts, Y nach OBEN, vom Bediener weg)
// in Maschinenkoordinaten und zurück.
QPointF operatorToMachine(const MachineSetup& setup, const QPointF& operatorUp);
QPointF machineToOperator(const MachineSetup& setup, const QPointF& machine);

// Wie oben, aber mit den Korrekturfaktoren der Kalibrierung: Die Maschinenkoordinate wird mit correctionX/Y
// multipliziert (G-Code) bzw. durch sie geteilt (Rückrechnung für die Anzeige).
QPointF operatorToMachineCorrected(const MachineSetup& setup, const QPointF& operatorUp);
QPointF machineToOperatorCorrected(const MachineSetup& setup, const QPointF& machine);

// Liegt die waagerechte Richtung des Bedieners auf der Maschinenachse X (Aufstellung 0° und 180°) oder auf Y (90°, 270°)?
bool operatorHorizontalIsMachineX(const MachineSetup& setup);

// Dasselbe Profil ohne Korrektur (für die Kalibrierung selbst und fremde NC-Dateien).
MachineSetup withoutCorrection(MachineSetup setup);

// Arbeitsbereich aus Sicht des Bedieners (Breite = waagerecht, Höhe = senkrecht auf dem Schirm).
// Bei Aufstellung 0° und 180° liegt X waagerecht, bei 90° und 270° ist es vertauscht.
QSizeF workAreaInOperatorView(const MachineSetup& setup);

// Passt der Auftrag (umschließendes Rechteck in mm, Sicht des Bedieners) in den Arbeitsbereich?
// Eine kleine Toleranz fängt Rundungsfehler beim Drehen ab.
bool fitsWorkArea(const QSizeF& boundsMm, const MachineSetup& setup);

// Richtwert für den Überscan in mm: Strecke, die die Achsen zum Beschleunigen auf den Vorschub brauchen
// (v² / 2a), für Zeilen im gegebenen Winkel (Sicht des Bedieners). 0, wenn die Beschleunigung unbekannt ist.
double overscanRecommendationMm(const MachineSetup& setup, double angleDeg, double speedMmPerMin);

// Vorgabe-Pixelgröße aus der Laserauflösung (25,4 / dpi).
double defaultPixelSizeMm(const MachineSetup& setup);

// JSON mit Versionsnummer. Beim Lesen fehlende Felder: Vorgabewerte; ungültige Werte werden korrigiert.
// Ohne Namen oder mit unbekannter Version (> 1) gibt es nichts zurück.
QJsonObject machineSetupToJson(const MachineSetup& setup);
std::optional<MachineSetup> machineSetupFromJson(const QJsonObject& json);

}  // namespace ld::core
