// Größe, Drehung und Spiegelung des geladenen Bildes (reine Rechnung, ohne Fenster).
//
// Alle Längen sind Millimeter aus Sicht des Bedieners (Ursprung oben links, Y nach unten).
// "Breite" und "Höhe" gehören zum ungedrehten Bild. Gedreht wird um die Mitte des Bildes;
// positive Gradzahlen drehen im Uhrzeigersinn (so, wie man es auf dem Bildschirm sieht).
// Gespiegelt wird vor dem Drehen, im Koordinatensystem des Bildes.
#pragma once

#include <QSize>
#include <QSizeF>

namespace ld::core {

struct ObjectTransform {
    double widthMm = 0.0;
    double heightMm = 0.0;
    double rotationDeg = 0.0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    // Lage der Grafik zum Nullpunkt (mm, Sicht des Bedieners: X nach rechts, Y nach unten): Der Job-Ursprung der
    // Grafik liegt um diesen Versatz neben dem Nullpunkt. 0 = Job-Ursprung genau auf dem Nullpunkt (planung.md 3.24).
    double offsetXMm = 0.0;
    double offsetYMm = 0.0;

    friend bool operator==(const ObjectTransform&, const ObjectTransform&) = default;
};

// Startgröße eines frisch geladenen Bildes: Bildpixel * Pixelgröße, ungedreht, nicht gespiegelt.
// Ungültige Eingabe (Pixelzahl <= 0 oder Pixelgröße <= 0) ergibt Breite und Höhe 0.
ObjectTransform initialTransform(const QSize& imagePixels, double mmPerPixel);

// Neue Breite setzen. Bei keepAspect folgt die Höhe im alten Seitenverhältnis.
// Ist die alte Höhe oder Breite 0, bleibt das Seitenverhältnis unbestimmt und die Höhe
// unverändert. Die Breite muss > 0 sein, sonst kommt der alte Wert unverändert zurück.
ObjectTransform withWidth(const ObjectTransform& t, double newWidthMm, bool keepAspect);

// Wie withWidth, nur für die Höhe.
ObjectTransform withHeight(const ObjectTransform& t, double newHeightMm, bool keepAspect);

// Umschließendes Rechteck des gedrehten Bildes (Breite und Höhe in mm).
// Spiegeln ändert es nicht.
QSizeF boundingSize(const ObjectTransform& t);

}  // namespace ld::core
