// SVG-Import für Full und Cutting (siehe docs/planung.md 3.14).
//
// Die SVG-Datei wird mit nanosvg gelesen (Pfade, Füllungen, Striche; kein Text, keine Verläufe).
// Daraus entstehen
//   * für Full ein Schwarzweißbild der Füllungen (renderSvgFill), das wie ein Rasterbild
//     vektorisiert wird (so gelten Zeichenreihenfolge und "weiß löscht" von selbst), und
//   * für Cutting Streckenzüge aus den Strichen (Mittellinie) und/oder den Umrissen der Füllungen.
// Für "Linie" wird die Datei mit Qt SVG als Raster gezeichnet (loadImageFile).
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <memory>
#include <vector>

#include "core/imagesettings.h"
#include "core/vectorize.h"

namespace ld::core {

struct SvgSubpath {
    std::vector<QPointF> bezier;   // 3n+1 Punkte: Start, (Stützpunkt 1, Stützpunkt 2, Ende) ...
    bool closed = false;
};

struct SvgShape {
    std::vector<SvgSubpath> subpaths;
    bool hasFill = false;      // einfarbige Füllung (Verläufe gelten als "keine Füllung")
    QColor fill;               // inklusive Deckkraft (Alpha)
    bool fillEvenOdd = false;
    bool hasStroke = false;    // einfarbiger Strich
    QColor stroke;
    double strokeWidth = 0.0;  // in SVG-Einheiten (px)
    Qt::PenJoinStyle join = Qt::MiterJoin;
    Qt::PenCapStyle cap = Qt::FlatCap;
    double miterLimit = 4.0;
};

struct SvgModel {
    double width = 0.0;        // Größe in px (96 dpi), wie nanosvg sie aus der Datei bestimmt
    double height = 0.0;
    std::vector<SvgShape> shapes;   // in Zeichenreihenfolge
    bool hasText = false;      // die Datei enthält Text, den nanosvg nicht liest

    QSizeF sizeMm() const { return QSizeF(width * 25.4 / 96.0, height * 25.4 / 96.0); }
};

// Liest eine SVG-Datei. Bei einem Fehler ist das Ergebnis leer und error gefüllt.
std::shared_ptr<const SvgModel> parseSvgFile(const QString& path, QString* error = nullptr);

// Zeichnet die Füllungen (und bei strokesAsArea die Striche in ihrer Breite) schwarz auf weiß in der
// Größe size (Format_Grayscale8). Helle Farben zeichnen weiß, Teile unter 50 % Deckkraft werden ignoriert.
QImage renderSvgFill(const SvgModel& model, bool strokesAsArea, const QSize& size);

// Streckenzüge für Cutting. Koordinaten in einem Raum der Größe space (wie Rasterpixel); die Kurven werden
// mit der Genauigkeit toleranceMm zerlegt (widthMm = Breite des Auftrags). Nur dunkle, ausreichend
// deckende Teile; geschlossene Umrisse bekommen ihre Verschachtelung (innere zuerst).
std::vector<Contour> svgCutContours(const SvgModel& model, const SvgSettings& settings, double toleranceMm,
                                    double widthMm, const QSizeF& space);

}  // namespace ld::core
