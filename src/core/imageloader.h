// Bilddateien und Zwischenablage-Bilder einlesen (Raster: alle Formate, die Qt auf dem System lesen kann, zum
// Beispiel jpg, png, bmp, gif, ico, ppm/pgm/pbm und mit dem Qt-Modul Image Formats auch tiff, webp, tga; dazu svg).
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <memory>

#include "core/svgmodel.h"

namespace ld::core {

struct LoadResult {
    QImage image;   // leer, wenn das Laden fehlschlug
    QString error;  // Fehlermeldung für den Bediener, leer bei Erfolg
    std::shared_ptr<const SvgModel> svg;   // nur bei SVG-Dateien (für Full und Cutting)
    bool ok() const { return !image.isNull(); }
};

// Legt transparente Bereiche auf Weiß (Transparenz gilt als weiß, siehe planung.md 3.4)
// und liefert ein deckendes Bild im Format RGB32. Ein leeres Bild bleibt leer.
QImage flattenOnWhite(const QImage& source);

// Dateiendungen (klein, ohne Punkt), die sich laden lassen, in fester Reihenfolge (jpg, jpeg, png, bmp, svg zuerst).
// Sie hängen von den installierten Qt-Plugins ab; "svg" ist immer dabei. Für den Dateidialog.
QStringList supportedImageSuffixes();

// Liest eine Bilddatei. EXIF-Drehung (Handyfotos) wird berücksichtigt. Eine SVG-Datei wird mit
// Qt SVG in der Größe aus der Datei und mit pixelSizeMm als Rasterabstand gezeichnet (für "Linie");
// zusätzlich liefert LoadResult die Vektordaten (nanosvg) für Full und Cutting.
LoadResult loadImageFile(const QString& path, double pixelSizeMm = 0.0635);

}  // namespace ld::core
