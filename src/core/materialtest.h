// Materialtest (siehe docs/planung.md 3.20): Eine Tabelle aus 3×3, 4×4 oder 5×5 Feldern mit Motiven. Die Power
// ändert sich von Spalte zu Spalte, die Speed von Zeile zu Zeile. Über den Spalten und links der Zeilen stehen die
// Werte als Zahlen (eine einfache Strichschrift für Ziffern), die mitgraviert werden.
//
// Der G-Code entsteht aus Teilaufträgen: Jedes Feld und jede Zahl ist ein eigener kleiner Auftrag mit eigenem
// Speed und eigener Power (GcodeJob mit Lage und ohne Kopf/Ende); Kopf und Ende kommen nur einmal.
#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <vector>

#include "core/engraving.h"
#include "core/gcode.h"
#include "core/machinesetup.h"

namespace ld::core {

enum class TestMotif {
    CircleFilled,    // Kreis gefüllt
    CircleOutline,   // Kreis (Umriss, eine Linie)
    SquareFilled,    // Quadrat gefüllt
    SquareOutline,   // Quadrat (Umriss, eine Linie)
    SquareGradient,  // Quadrat mit diagonalen Linien, die von links nach rechts immer enger werden
};

struct MaterialTestSpec {
    int cells = 5;                      // 3, 4 oder 5 (Spalten und Zeilen)
    TestMotif motif = TestMotif::SquareFilled;
    double sizeMm = 10.0;               // Kantenlänge des Motivs (5, 10 oder 15)
    EngraveMode mode = EngraveMode::Line;
    std::vector<double> speeds;         // mm/min je Zeile (oben nach unten)
    std::vector<double> powers;         // % je Spalte (links nach rechts)
    std::vector<QString> speedLabels;   // Text links jeder Zeile (der Aufrufer formatiert nach der Einheit)
    std::vector<QString> powerLabels;   // Text über jeder Spalte
    double labelSpeedMmPerMin = 1500.0; // Beschriftung: Speed
    double labelPowerPercent = 30.0;    // Beschriftung: Power
    double labelHeightMm = 3.0;         // Höhe der Ziffern
    JobOrigin origin = JobOrigin::TopLeft;
    double pixelSizeMm = 0.0635;
    double overscanMm = 1.0;
};

// Lage aller Teile in Millimetern; Ursprung ist die obere linke Ecke der ganzen Zeichnung (Bedienersicht, y nach unten).
struct MaterialTestLayout {
    QSizeF total;                       // Größe der ganzen Zeichnung (Tabelle mit Beschriftung)
    double gapMm = 2.0;                 // Abstand zwischen den Feldern
    double tableX = 0.0, tableY = 0.0;  // obere linke Ecke der Tabelle (hinter der Beschriftung)
    QRectF cell(int column, int row, double sizeMm) const;
    std::vector<QRectF> speedLabels;    // Platz je Spaltenbeschriftung (Power, oben)
    std::vector<QRectF> powerLabels;    // Platz je Zeilenbeschriftung (Speed, links)
};

// Rechnet die Lage aus (Beschriftungsbreiten aus dem Text, Abstand 40 % der Motivgröße, mindestens 2 mm).
MaterialTestLayout materialTestLayout(const MaterialTestSpec& spec);

// Ist die Spezifikation brauchbar? Leer = ja, sonst die Meldung für den Bediener.
QString validateMaterialTest(const MaterialTestSpec& spec);

// Rastergrafiken (Graustufen: 0 = brennen, 255 = leer) in der Pixelgröße des Lasers.
QImage motifRaster(TestMotif motif, double sizeMm, double pixelMm);
// Ziffern, Komma, Punkt und Minus in einer Strichschrift. Unbekannte Zeichen bleiben leer.
QImage textRaster(const QString& text, double heightMm, double pixelMm);
// Dieselbe Strichschrift als Streckenzüge in mm (Ursprung oben links, Y nach unten), für Vektorgrafiken.
// Auch die Buchstaben X und Y sind enthalten.
std::vector<std::vector<QPointF>> textStrokes(const QString& text, double heightMm);
// Breite eines Textes in mm (ohne Raster zu zeichnen).
double textWidthMm(const QString& text, double heightMm, double pixelMm);

// Verkleinerte Darstellung der ganzen Zeichnung (für die Vorschau im Dialog), pxPerMm Bildpunkte je mm.
QImage materialTestPreview(const MaterialTestSpec& spec, double pxPerMm);

// Der ganze G-Code. Beim Fehler enthält die Meldung den Grund (leer bei Abbruch).
GcodeResult generateMaterialTestGcode(const MaterialTestSpec& spec, const MachineSetup& machine, const GcodeSink& sink,
                                      const std::atomic_bool* cancel = nullptr,
                                      const std::function<void(double)>& progress = {});

// Ein Auftrag mit der Größe der ganzen Zeichnung, für den Rahmen (Rahmen abfahren) und die Prüfung des Arbeitsbereichs.
GcodeJob materialTestFrameJob(const MaterialTestSpec& spec, const MachineSetup& machine);

}  // namespace ld::core
