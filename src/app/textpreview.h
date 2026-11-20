// Vorschau im Dialog "Schrift" (siehe docs/planung.md 3.22): zeigt die Umrisse des Textes in Millimetern, mit
// Linealen wie auf der Arbeitsfläche (Strich an der Mausposition), Zoom mit dem Mausrad (auf die Mausposition),
// Verschieben mit gedrückter Maustaste und Fadenkreuz als Mauszeiger. Der Nullpunkt der Lineale ist die obere
// linke Ecke des Textes. Solange der Bediener weder gezoomt noch verschoben hat, passt sich die Ansicht dem Text an.
#pragma once

#include <QGraphicsView>

#include "core/textmodel.h"
#include "rulerwidget.h"

class QGraphicsPathItem;
class QGraphicsRectItem;

namespace ld::app {

class TextPreview : public QGraphicsView, public RulerSource {
    Q_OBJECT

public:
    explicit TextPreview(QWidget* parent = nullptr);

    // Zeigt die Form; eine leere Form zeigt stattdessen den Hinweistext (message).
    void setShape(const ld::core::TextShape& shape, const QString& message);
    // Wie setShape, aber für Linien (Kalibrierung): Der Pfad wird mit dünner Linie gezeichnet statt gefüllt.
    void setStrokeShape(const QPainterPath& path, const QSizeF& sizeMm, const QString& message);
    void zoomIn();
    void zoomOut();
    // Passt die Ansicht an den Text an (und schaltet das automatische Anpassen wieder ein).
    void fit();

    double pixelsPerMm() const override;
    QPointF rulerOffsetMm() const override { return {}; }
    bool mouseInside() const override { return mouseInside_; }
    QPoint mousePos() const override { return mousePos_; }

signals:
    // Mausposition in mm ab der oberen linken Ecke des Textes; inside = false: Maus draußen.
    void mouseMoved(bool inside, const QPointF& mm);
    // Zoomfaktor hat sich geändert (für die Anzeige).
    void zoomChanged(double pixelsPerMm);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void changeEvent(QEvent* event) override;

private:
    void zoomByFactor(double factor);
    void layoutRulers();
    void updateRulers();

    QGraphicsRectItem* paperItem_ = nullptr;
    QGraphicsPathItem* pathItem_ = nullptr;
    QGraphicsRectItem* boundsItem_ = nullptr;
    RulerWidget* horizontalRuler_ = nullptr;
    RulerWidget* verticalRuler_ = nullptr;
    ld::core::TextShape shape_;
    QString message_;
    bool autoFit_ = true;
    bool panning_ = false;
    QPoint lastPanPos_;
    QPoint mousePos_;
    bool mouseInside_ = false;
};

}  // namespace ld::app
