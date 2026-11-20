// G-Code für Rasterzeilen ("Linie", Meilenstein M3), siehe docs/planung.md 3.10 und 9.4.
//
// Ablauf: Das Arbeitsraster (0 = brennen, 255 = nicht brennen, Graustufen = anteilige Leistung)
// wird in Zeilen abgetastet. Die Zeilen laufen im eingestellten Winkel über die Arbeitsfläche
// (Sicht des Bedieners); Drehung und Spiegelung des Bildes werden beim Abtasten eingerechnet.
// Abschnitte gleicher Leistung werden zusammengefasst (Run-Length), leere Zeilen und leere
// Zeilenenden übersprungen. Die Koordinaten werden zuletzt in die Aufstellung des Lasers umgerechnet.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <atomic>
#include <functional>

#include "core/engraving.h"
#include "core/imagesettings.h"
#include "core/machinesetup.h"
#include "core/objecttransform.h"
#include "core/svgmodel.h"
#include <memory>

namespace ld::core {

struct GcodeJob {
    QImage raster;              // Graubild (Format_Grayscale8), siehe imageprocessing.h
    ObjectTransform transform;  // Größe in mm, Drehung, Spiegelung
    Operation operation;
    JobOrigin origin = JobOrigin::TopLeft;
    MachineSetup machine;
    double pixelSizeMm = 0.0635;
    VectorizeSettings vectorize;   // nur für Full und Cutting
    std::shared_ptr<const SvgModel> svg;   // bei SVG-Dateien: Cutting liest die Striche direkt daraus
    SvgSettings svgSettings;
    // Graustufen-Leistung: nur wenn das Raster mit dem Verfahren "Graustufen-Leistung" entstand (sonst ist es schwarzweiß).
    bool grayPowerEnabled = false;
    // Full und Cutting: Das Raster ist gedithert oder hat Graustufen. Dann ist jede Fläche gleicher Leistung
    // (auch ein einzelner Punkt) eine eigene Fläche mit pixelgenauem Umriss statt potrace (planung.md 3.18).
    bool pixelAreas = false;
    GrayPowerSettings grayPower;
    QString title;              // z. B. Bildname, für den Kopfkommentar
    // Teilaufträge (zum Beispiel beim Materialtest, dessen Felder zu einem G-Code zusammengesetzt werden):
    bool placed = false;                 // true: Lage des Mittelpunkts gegenüber dem Ursprung ist vorgegeben ...
    QPointF centerFromOriginMm;          // ... in Bedienersicht (x nach rechts, y nach unten), statt aus origin berechnet
    bool omitHeader = false;             // ohne Kopf (Start-Code, M4/M3): schaltet stattdessen nur S0
    bool omitFooter = false;             // ohne Ende (Ende-Code, M5)
    QString note;                        // zusätzlicher Kommentar im Kopf
};

struct GcodeStats {
    double seconds = 0.0;        // Richtwert aus Länge und Vorschub, ohne Beschleunigung
    double burnLengthMm = 0.0;   // Strecke mit Vorschub (G1)
    double travelLengthMm = 0.0; // Strecke im Eilgang (G0)
    long long lines = 0;         // Zeilen G-Code
    long long bytes = 0;
};

struct GcodeResult {
    bool ok = false;
    bool cancelled = false;
    QString error;      // Meldung für den Bediener, leer bei Erfolg
    GcodeStats stats;
};

// Nimmt Stücke des G-Codes entgegen. Gibt false zurück, wenn das Schreiben fehlschlug.
using GcodeSink = std::function<bool(const QByteArray&)>;

// Erzeugt den G-Code. Ist sink leer, wird nur gezählt (Zeitschätzung) und kein Text gebaut.
// progress (0..1) wird gelegentlich aufgerufen und darf leer sein.
// Fehler: Auftrag größer als der Arbeitsbereich, leeres Raster, ungültige Werte.
GcodeResult generateGcode(const GcodeJob& job, const GcodeSink& sink = {},
                          const std::atomic_bool* cancel = nullptr,
                          const std::function<void(double)>& progress = {});

struct FrameResult {
    bool ok = false;
    QString error;
    QByteArray gcode;
};

// Rahmen ("Test Size"): Das umschließende Rechteck des Auftrags (nach Job-Ursprung, Drehung und
// Aufstellung) wird mit speedMmPerMin und powerPercent (Vorgabe im Panel: 0 %) einmal abgefahren,
// danach zurück zum Nullpunkt. Gleiche Prüfungen wie beim Auftrag (Arbeitsbereich).
FrameResult generateFrameGcode(const GcodeJob& job, double speedMmPerMin, double powerPercent);

// Rahmen um ein Rechteck (NC-Auftrag, planung.md 3.21): sceneRect in Bedienersicht (x nach rechts, y nach unten) mit dem
// Nullpunkt der Datei als Ursprung, also die Wege so, wie die Anzeige sie hat. Prüft die Größe gegen den Arbeitsbereich.
FrameResult generateFrameGcodeForRect(const MachineSetup& machine, const QRectF& sceneRect, double speedMmPerMin,
                                      double powerPercent);

// Zahl für den G-Code: 3 Nachkommastellen, ohne überflüssige Nullen ("3.175", "-1", "0").
QByteArray formatGcodeNumber(double value);

// Zeit als "h:mm:ss" bzw. "m:ss".
QString formatDuration(double seconds);

}  // namespace ld::core
