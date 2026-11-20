// Hilfen für die Anzeige: Farbumsetzung des Rasterbildes (Graubild mit 0 = brennen, 255 = Papier).
#pragma once

#include <QColor>
#include <QImage>

namespace ld::app {

// Färbt ein Graubild so ein, dass 0 die Farbe "burn" und 255 die Farbe "paper" hat (dazwischen
// gemischt). Bei Schwarz/Weiß wird das Bild unverändert zurückgegeben (schnell, keine Kopie).
inline QImage recolorRaster(const QImage& gray, const QColor& burn, const QColor& paper)
{
    if (gray.isNull() || gray.format() != QImage::Format_Grayscale8 ||
        (burn == QColor(0, 0, 0) && paper == QColor(255, 255, 255))) {
        return gray;
    }
    QImage indexed(gray.size(), QImage::Format_Indexed8);
    QList<QRgb> table;
    table.reserve(256);
    for (int g = 0; g < 256; ++g) {
        const double f = g / 255.0;
        table.append(qRgb(static_cast<int>(burn.red() + (paper.red() - burn.red()) * f + 0.5),
                          static_cast<int>(burn.green() + (paper.green() - burn.green()) * f + 0.5),
                          static_cast<int>(burn.blue() + (paper.blue() - burn.blue()) * f + 0.5)));
    }
    indexed.setColorTable(table);
    for (int y = 0; y < gray.height(); ++y) {
        std::copy(gray.constScanLine(y), gray.constScanLine(y) + gray.width(), indexed.scanLine(y));
    }
    return indexed;
}

// Für die Arbeitsfläche: Nur das, was gebrannt wird, ist sichtbar und deckend in der Farbe "burn"; das Weiß des Rasters
// (255) ist ganz durchsichtig (dahinter liegt der weiße, halb durchsichtige Grund mit dem Raster der Arbeitsfläche).
// Graustufen dazwischen werden entsprechend durchsichtig (Dithering und Graustufen bleiben erkennbar). Jedes Bild
// (auch ein Farbbild) wird dafür zuerst in Graustufen umgerechnet.
inline QImage overlayRaster(const QImage& source, const QColor& burn)
{
    if (source.isNull()) return QImage();
    const QImage gray = source.format() == QImage::Format_Grayscale8 ? source : source.convertToFormat(QImage::Format_Grayscale8);
    QImage indexed(gray.size(), QImage::Format_Indexed8);
    QList<QRgb> table;
    table.reserve(256);
    for (int g = 0; g < 256; ++g) table.append(qRgba(burn.red(), burn.green(), burn.blue(), 255 - g));
    indexed.setColorTable(table);
    for (int y = 0; y < gray.height(); ++y) {
        std::copy(gray.constScanLine(y), gray.constScanLine(y) + gray.width(), indexed.scanLine(y));
    }
    return indexed;
}

}  // namespace ld::app
