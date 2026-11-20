// Bildverarbeitung für den Import: Graustufen, Skalieren, Schärfen, Helligkeit/Kontrast/Gamma,
// Schwellenwert, Dithering, Histogramm und Otsu-Verfahren. Reine Rechnung ohne Fenster.
//
// Ablauf in processImage():
//   Ausschnitt -> Graustufen -> auf Arbeitsraster skalieren -> Schärfen
//   -> Helligkeit/Kontrast/Gamma -> Invertieren -> Umwandlung (Schwelle/Dithering/Graustufen)
//
// Ergebnisbilder sind 8-Bit-Graubilder (QImage::Format_Grayscale8). Bei Schwellenwert und
// Dithering enthalten sie nur 0 (schwarz = Laser brennt) und 255 (weiß).
#pragma once

#include <QImage>
#include <QRect>
#include <QSizeF>
#include <array>
#include <atomic>

#include "core/imagesettings.h"
#include "core/objecttransform.h"

namespace ld::core {

// Obergrenze für das Arbeitsraster (Pixel). 400 x 500 mm bei 400 dpi sind etwa 50 Megapixel.
inline constexpr long long kMaxRasterPixels = 64LL * 1000 * 1000;

using Histogram = std::array<int, 256>;

// Alle Funktionen, die lange rechnen, prüfen "cancel" zeilenweise und geben bei gesetztem
// Flag ein leeres Bild zurück. cancel darf nullptr sein.

// Graustufen mit Gewichten 0,30 / 0,59 / 0,11 (Rec. 601). Nur der Ausschnitt "area" wird gelesen.
QImage toGrayscale(const QImage& source, const QRect& area, const std::atomic_bool* cancel = nullptr);

// Ändert die Größe eines Graubildes (Mittelwert beim Verkleinern, bilinear beim Vergrößern).
QImage resampleGray(const QImage& gray, const QSize& target, const std::atomic_bool* cancel = nullptr);

// Unscharf maskieren; amountPercent 0 ändert nichts.
QImage sharpenGray(const QImage& gray, int amountPercent, const std::atomic_bool* cancel = nullptr);

// Tabelle für Helligkeit, Kontrast, Gamma und Invertieren (in dieser Reihenfolge).
std::array<unsigned char, 256> toneTable(const ImageSettings& settings);
void applyTable(QImage& gray, const std::array<unsigned char, 256>& table);

Histogram histogramOf(const QImage& gray);

// Otsu-Verfahren: Schwelle T (0..255), die Vordergrund und Hintergrund am besten trennt.
// Pixel mit Wert < T gelten als dunkel. Ein leeres Histogramm ergibt 128.
int otsuThreshold(const Histogram& histogram);

// Schwarz (0) für Werte < threshold, sonst weiß (255).
QImage thresholdGray(const QImage& gray, int threshold);

// Dithering. method muss FloydSteinberg, Atkinson oder Bayer sein.
QImage ditherGray(const QImage& gray, ConversionMethod method, const std::atomic_bool* cancel = nullptr);

// Dithering mit größeren Punkten: Das Graubild wird in Blöcke von cell x cell Pixeln geteilt
// (am Rand kleinere), pro Block gemittelt, auf dem gröberen Raster mit ditherGray() gerechnet
// und wieder auf die Originalgröße vergrößert. cell = 1 entspricht ditherGray().
QImage ditherBlocks(const QImage& gray, ConversionMethod method, int cell,
                    const std::atomic_bool* cancel = nullptr);

struct ProcessResult {
    QImage raster;              // Ergebnis; leer bei Abbruch oder ungültiger Eingabe
    Histogram histogram{};      // Grauwerte vor der Umwandlung (siehe oben)
    Histogram originalHistogram{};   // Grauwerte des Originals (Ausschnitt, skaliert), vor Schärfen und Tonwerten
    int otsu = 128;             // Otsu-Schwelle dieses Bildes
    int usedThreshold = 128;    // beim Schwellenwert tatsächlich benutzte Schwelle
    bool clamped = false;       // Raster wurde wegen kMaxRasterPixels verkleinert
    bool cancelled = false;
};

// Größe des Arbeitsrasters in Pixeln für eine Größe in mm und eine Pixelgröße (mindestens 1 x 1,
// begrenzt auf kMaxRasterPixels; clamped zeigt die Begrenzung an).
QSize rasterSizeFor(const QSizeF& sizeMm, double pixelSizeMm, bool* clamped = nullptr);

// Die ganze Verarbeitung. sizeMm ist die Größe des (ungedrehten) Auftrags in mm.
ProcessResult processImage(const QImage& source, const ImageSettings& settings, const QSizeF& sizeMm,
                           const std::atomic_bool* cancel = nullptr);

// Umschließendes Rechteck (in Pixeln des Originals) von allem, was der Laser mit diesen Einstellungen brennt:
// jedes Pixel des Ergebnisrasters, das nicht weiß ist (bei Graustufen-Leistung: jedes nicht weiße Pixel).
// Der Ausschnitt in den Einstellungen wird ignoriert, es zählt das ganze Bild. Gerechnet wird mit allen
// Schritten der Verarbeitung (Helligkeit, Kontrast, Invertieren, Schwellenwert, Dithering) im Maßstab des
// Originals, ohne Rand und ohne Ausnahmen (jedes einzelne Pixel zählt). Leeres Rechteck: es wird nichts
// gebrannt. binaryOnly: wie bei Full und Cutting nur Schwellenwert (Dithering und Graustufen entfallen).
QRect contentBounds(const QImage& source, const ImageSettings& settings, bool binaryOnly);

// Größe des Auftrags nach einer Änderung des Ausschnitts: Der Maßstab (mm pro Quellpixel)
// bleibt gleich. base gehört zum Ausschnitt baseCrop, percent skaliert zusätzlich (100 = unverändert).
ObjectTransform transformForCrop(const ObjectTransform& base, const QRect& baseCrop,
                                 const QRect& newCrop, double percent = 100.0);

}  // namespace ld::core
