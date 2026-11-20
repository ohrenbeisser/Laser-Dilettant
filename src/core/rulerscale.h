// Teilung von Lineal und Raster: Welche Abstände in mm passen zum Zoom?
#pragma once

namespace ld::core {

struct RulerScale {
    double majorMm = 10.0;      // Abstand der beschrifteten Striche
    int subdivisions = 10;      // so viele Teile hat ein Hauptabstand (kleine Striche)
    double minorMm() const { return majorMm / subdivisions; }
};

// Wählt "runde" Abstände (1, 2, 5 mal Zehnerpotenz), sodass zwei beschriftete Striche
// mindestens minMajorPixels auseinanderliegen. pixelsPerMm <= 0 ergibt die Vorgabe (10 mm).
RulerScale rulerScaleFor(double pixelsPerMm, double minMajorPixels = 70.0);

}  // namespace ld::core
