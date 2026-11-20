// Lineal am Rand einer QGraphicsView (Arbeitsfläche und Textvorschau).
//
// Das Lineal zeichnet sich selbst anhand der Ansicht (Zoom und Verschiebung) und zeigt die Mausposition als
// Strich. Die Szene der Ansicht zählt in Millimetern; die Zahlen im Lineal beginnen bei rulerOffsetMm().
// Die Ansicht liefert die nötigen Werte über die kleine Schnittstelle RulerSource.
#pragma once

#include <QPoint>
#include <QPointF>
#include <QWidget>

class QGraphicsView;

namespace ld::app {

constexpr int kRulerThickness = 22;   // Breite des Lineals in Bildschirmpixeln

class RulerSource {
public:
    virtual ~RulerSource() = default;
    virtual double pixelsPerMm() const = 0;            // aktueller Zoom
    // Zahl im Lineal = Vorzeichen * Szenenposition + Verschiebung (je Achse, in mm). Das Vorzeichen (+1 oder -1)
    // bestimmt die Zählrichtung; der Nullpunkt des Lineals ist der Ursprung des Auftrags (planung.md 3.23).
    virtual QPointF rulerOffsetMm() const = 0;
    virtual QPointF rulerSign() const { return {1.0, 1.0}; }
    virtual bool mouseInside() const = 0;
    virtual QPoint mousePos() const = 0;               // Bildschirmpixel im Anzeigebereich
};

class RulerWidget : public QWidget {
public:
    RulerWidget(Qt::Orientation orientation, QGraphicsView* view, const RulerSource* source);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Qt::Orientation orientation_;
    QGraphicsView* view_;
    const RulerSource* source_;
};

}  // namespace ld::app
