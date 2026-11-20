// Der grüne Fortschritt der Simulation: Eine halbtransparente Ebene, auf der alles, was der Laser
// schon gebrannt hat, grün erscheint (siehe docs/planung.md 3.11 und Abschnitt 8).
//
// Es wird nur das neu hinzugekommene Stück gezeichnet (planung.md 8, "Technik"). Die Ebene ist ein
// Bild im Maßstab der Szene (mm), höchstens 2048 Pixel auf der langen Seite.
#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>

#include "core/pathdata.h"

namespace ld::app {

class ProgressOverlay {
public:
    static constexpr double kOpacity = 0.45;   // Deckkraft beim Zeichnen auf der Arbeitsfläche

    // Neue, leere Ebene für den Szenenausschnitt rect; lineWidthMm ist die Breite der Gravurspur.
    void reset(const QRectF& sceneRect, double lineWidthMm);
    void clear();
    bool isActive() const { return !image_.isNull(); }

    // Zeichnet die Gravur-Wegstücke [first, last) der Daten grün dazu.
    void paint(const ld::core::PathData& data, size_t first, size_t last);

    void setColor(const QColor& color) { color_ = color; }
    const QImage& image() const { return image_; }
    const QRectF& rect() const { return rect_; }

private:
    QImage image_;
    QRectF rect_;
    double scale_ = 1.0;   // Bildpixel pro mm
    double lineWidthMm_ = 0.0635;
    QColor color_ = QColor(0x2e, 0x9e, 0x4f);
};

}  // namespace ld::app
