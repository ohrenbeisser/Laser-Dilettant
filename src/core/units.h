// Umrechnung zwischen Auflösung (dpi), Pixelgröße (mm) und Bildgröße (mm).
//
// Hintergrund (siehe docs/planung.md, Begriffe): Die "Pixelgröße" ist der Abstand
// zweier Laserpunkte in Millimetern. Ein Laser mit 400 dpi hat 25,4 / 400 = 0,0635 mm.
// Beim Laden eines Bildes wird jedes Bildpixel zu einem Laserpunkt; ein Bild mit
// 1000 Pixeln Breite ist dann 1000 * 0,0635 = 63,5 mm breit.
#pragma once

namespace ld::core {

// Ein Zoll in Millimetern (Definition, kein Messwert).
inline constexpr double kMmPerInch = 25.4;

// Pixelgröße in mm für eine Auflösung in dpi (Punkte pro Zoll).
// Ungültige Eingabe (dpi <= 0) ergibt 0.0.
double mmPerPixelFromDpi(double dpi);

// Umkehrung: Auflösung in dpi für eine Pixelgröße in mm.
// Ungültige Eingabe (mm <= 0) ergibt 0.0.
double dpiFromMmPerPixel(double mmPerPixel);

// Größe in mm für eine Anzahl Pixel bei gegebener Pixelgröße.
// Negative Pixelzahl oder Pixelgröße <= 0 ergibt 0.0.
double sizeInMm(int pixelCount, double mmPerPixel);

}  // namespace ld::core
