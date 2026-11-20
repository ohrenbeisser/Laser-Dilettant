// Vektorisierung für Full und Cutting (siehe docs/planung.md 3.14).
//
// Aus dem Schwarzweiß-Raster (0 = brennen) werden mit potrace die Umrisse der schwarzen Flächen
// bestimmt und als Streckenzüge geliefert (Kurven sind bereits in kurze Strecken zerlegt).
// potrace ist eine C-Bibliothek unter GPL; sie wird nur in vectorize.cpp benutzt.
#pragma once

#include <QImage>
#include <QPointF>
#include <array>
#include <atomic>
#include <vector>

#include "core/imagesettings.h"

namespace ld::core {

// Ein geschlossener Umriss. Der letzte Punkt ist NICHT gleich dem ersten (die Schließung ist implizit).
struct Contour {
    std::vector<QPointF> points;
    int parent = -1;   // Index des umschließenden Umrisses in der Liste, -1 = außen
    bool closed = true;   // false: offener Streckenzug (SVG-Strich)
    int depth = 0;     // 0 = äußerer Umriss einer Fläche, 1 = Loch darin, 2 = Insel im Loch ...
    int powerS = -1;   // Leistung (S-Wert) dieser Fläche; -1 = die Leistung des Auftrags (bei Graustufen je Stufe verschieden)

    bool isHole() const { return depth % 2 == 1; }
};

// Bestimmt Eltern und Tiefe der geschlossenen Umrisse (Umriss im Umriss). Offene bleiben außen.
// Ab sehr vielen Umrissen wird darauf verzichtet (zu langsam); alles bleibt dann "außen".
void assignNesting(std::vector<Contour>& contours);

struct VectorizeResult {
    std::vector<Contour> contours;   // Eltern stehen immer vor ihren Kindern
    bool ok = false;
    bool cancelled = false;
    QString error;
};

// raster: Graubild (Format_Grayscale8), Pixel < 128 gelten als schwarz.
// toleranceMm: größte Abweichung beim Zerlegen von Kurven; pixelMm: Kantenlänge eines Pixels.
// Die Punkte werden in Pixeleinheiten des Rasters geliefert (x nach rechts, y nach unten,
// Ursprung oben links), Pixel (i, j) belegt also [i, i+1] x [j, j+1].
VectorizeResult vectorizeRaster(const QImage& raster, const VectorizeSettings& settings, double toleranceMm,
                                double pixelMm, const std::atomic_bool* cancel = nullptr);

// Pixelgenaue Flächen für Dithering und Graustufen bei Full und Cutting (siehe docs/planung.md 3.18).
// Jede zusammenhängende Gruppe von Pixeln mit derselben Leistung ist eine Fläche; zusammenhängend heißt
// über gemeinsame Kanten (Pixel, die sich nur an einer Ecke berühren, sind getrennte Flächen). Auch ein
// einzelnes Pixel ist eine Fläche. Es gibt keine Glättung: Die Umrisse laufen genau an den Pixelkanten.
// powerOfGray: Leistung (S-Wert) für jeden Grauwert; 0 = wird nicht gebrannt (gehört zu keiner Fläche).
// Löcher sind Kinder ihrer Fläche (depth 1); Flächen innerhalb eines Lochs sind eigene Flächen (depth 0).
// Die Punkte liegen in Pixeleinheiten wie bei vectorizeRaster(); jeder Umriss hat die Leistung seiner Fläche
// (powerS). Gerade Strecken sind zu einem Stück zusammengefasst.
VectorizeResult vectorizePixelAreas(const QImage& raster, const std::array<int, 256>& powerOfGray,
                                    const std::atomic_bool* cancel = nullptr);

}  // namespace ld::core
