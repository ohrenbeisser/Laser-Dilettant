// Vorschau im Importdialog: zeigt das Ergebnis der Bildbearbeitung und dient zum Zuschneiden.
//
// Zwei Betriebsarten:
//   Ergebnis:    das berechnete Raster, in mm (Szene = Millimeter), Zoom mit Mausrad, Verschieben.
//   Zuschneiden: das Originalbild (Szene = Pixel des Originals) mit einem Rahmen, den man
//                aufziehen, verschieben und an Rändern/Ecken ziehen kann (siehe planung.md 3.9).
#pragma once

#include <QGraphicsView>

class QGraphicsPixmapItem;

namespace ld::app {

class PreviewView : public QGraphicsView {
    Q_OBJECT

public:
    explicit PreviewView(QWidget* parent = nullptr);

    // Ergebnis anzeigen. sizeMm ist die Größe des Auftrags in mm. fitToView: Zoom neu einpassen.
    void setResult(const QImage& raster, const QSizeF& sizeMm, bool fitToView);

    // Das Originalbild für den Zuschneide-Modus (wird nur dann gezeigt).
    void setSourceImage(const QImage& image);
    // Im Zuschneide-Modus das gerechnete Ergebnis (statt des Originals) zeigen. raster deckt das ganze Bild ab;
    // sourceSize ist die Größe des Originals in Pixeln (die Szene bleibt in Pixeln des Originals).
    void setCropBackdrop(const QImage& raster, const QSize& sourceSize);

    // Zuschneide-Modus ein/aus. Beim Einschalten ist initialCrop der erste Rahmen (Pixel des Originals).
    void setCropMode(bool on, const QRect& initialCrop = QRect());
    bool cropMode() const { return cropMode_; }
    QRect cropRect() const { return cropRect_.toRect(); }
    void setCropRect(const QRect& rect);

    void fit();
    void showOneToOne();

    // Zoom um den gegebenen Faktor um die Bildmitte (für die Knöpfe + und −).
    void zoomBy(double factor);

    // Zoom in Prozent (100 % = ein Rasterpixel pro Bildschirmpixel im Ergebnis-Modus).
    double zoomPercent() const;

signals:
    void cropChanged(const QRect& cropInSourcePixels);
    void zoomChanged();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
    enum class Drag { None, NewRect, Move, Resize };

    int edgesAt(const QPoint& viewPos) const;
    void updateCursorFor(const QPoint& viewPos);
    void updateSampling();
    QRectF currentBounds() const;
    QPointF clampedScenePoint(const QPoint& viewPos) const;
    void setCrop(const QRectF& rect);

    QGraphicsPixmapItem* resultItem_ = nullptr;
    QGraphicsPixmapItem* sourceItem_ = nullptr;
    QSize sourceSize_;          // Größe des Originals in Pixeln (Szene im Zuschneide-Modus)
    QSizeF resultSizeMm_;
    int resultPixelWidth_ = 0;

    bool cropMode_ = false;
    QRectF cropRect_;
    Drag drag_ = Drag::None;
    int dragEdges_ = None;
    QPointF dragStart_;
    QRectF dragOriginalRect_;
};

}  // namespace ld::app
