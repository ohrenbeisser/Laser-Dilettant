#include "textpreview.h"

#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace ld::app {

namespace {
constexpr double kMinZoom = 0.2;    // Bildschirmpixel pro mm
constexpr double kMaxZoom = 600.0;
constexpr double kZoomStep = 1.2;
constexpr double kSceneHalfMm = 5000.0;
constexpr double kFitMargin = 0.86;   // der Text füllt höchstens 86 % der Ansicht
}  // namespace

TextPreview::TextPreview(QWidget* parent) : QGraphicsView(parent)
{
    auto* scene = new QGraphicsScene(this);
    scene->setSceneRect(-kSceneHalfMm, -kSceneHalfMm, 2 * kSceneHalfMm, 2 * kSceneHalfMm);
    setScene(scene);

    // Weißes Blatt hinter dem Text: so sieht man die Umrisse in beiden Designs gleich gut.
    paperItem_ = scene->addRect(QRectF(), Qt::NoPen, Qt::white);
    paperItem_->setZValue(-1.0);
    pathItem_ = scene->addPath(QPainterPath(), Qt::NoPen, Qt::black);
    boundsItem_ = scene->addRect(QRectF());
    QPen boundsPen(QColor(0x2f, 0x80, 0xff));   // wie der gestrichelte blaue Rahmen auf der Arbeitsfläche
    boundsPen.setWidthF(1.5);
    boundsPen.setCosmetic(true);
    boundsPen.setStyle(Qt::DashLine);
    boundsItem_->setPen(boundsPen);
    boundsItem_->setZValue(1.0);

    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::CrossCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    setViewportMargins(kRulerThickness, kRulerThickness, 0, 0);
    horizontalRuler_ = new RulerWidget(Qt::Horizontal, this, this);
    verticalRuler_ = new RulerWidget(Qt::Vertical, this, this);
    layoutRulers();
    scale(5.0, 5.0);
    centerOn(0, 0);
}

double TextPreview::pixelsPerMm() const
{
    return transform().m11();
}

void TextPreview::setStrokeShape(const QPainterPath& path, const QSizeF& sizeMm, const QString& message)
{
    ld::core::TextShape shape;
    shape.path = path;
    shape.sizeMm = sizeMm;
    setShape(shape, message);
    QPen pen(Qt::black, 1.4);
    pen.setCosmetic(true);
    pathItem_->setPen(pen);
    pathItem_->setBrush(Qt::NoBrush);
}

void TextPreview::setShape(const ld::core::TextShape& shape, const QString& message)
{
    pathItem_->setPen(Qt::NoPen);
    pathItem_->setBrush(Qt::black);
    shape_ = shape;
    message_ = message;
    const bool has = !shape.isEmpty();
    const QRectF box = has ? QRectF(QPointF(0, 0), shape.sizeMm) : QRectF();
    paperItem_->setRect(box);
    paperItem_->setVisible(has);
    pathItem_->setPath(has ? shape.path : QPainterPath());
    boundsItem_->setRect(box);
    boundsItem_->setVisible(has);
    if (autoFit_) {
        fit();
    }
    viewport()->update();
}

void TextPreview::fit()
{
    autoFit_ = true;
    const QSizeF size = shape_.isEmpty() ? QSizeF(40.0, 15.0) : shape_.sizeMm;
    const double availableW = std::max(1, viewport()->width()) * kFitMargin;
    const double availableH = std::max(1, viewport()->height()) * kFitMargin;
    const double zoom = std::clamp(std::min(availableW / size.width(), availableH / size.height()), kMinZoom, kMaxZoom);
    resetTransform();
    scale(zoom, zoom);
    centerOn(size.width() / 2.0, size.height() / 2.0);
    updateRulers();
    emit zoomChanged(zoom);
}

void TextPreview::zoomByFactor(double factor)
{
    const double target = std::clamp(pixelsPerMm() * factor, kMinZoom, kMaxZoom);
    factor = target / pixelsPerMm();
    autoFit_ = false;
    scale(factor, factor);
    updateRulers();
    emit zoomChanged(pixelsPerMm());
}

void TextPreview::zoomIn() { zoomByFactor(kZoomStep); }
void TextPreview::zoomOut() { zoomByFactor(1.0 / kZoomStep); }

void TextPreview::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0) {
        zoomByFactor(std::pow(kZoomStep, steps));
    }
    event->accept();
}

void TextPreview::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    layoutRulers();
    if (autoFit_) {
        fit();
    }
}

void TextPreview::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    updateRulers();
}

void TextPreview::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPanPos_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);   // Handcursor erst bei gedrückter Maustaste
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void TextPreview::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_) {
        panning_ = false;
        viewport()->setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void TextPreview::mouseMoveEvent(QMouseEvent* event)
{
    mousePos_ = event->pos();
    mouseInside_ = true;
    if (panning_) {
        const QPoint delta = event->pos() - lastPanPos_;
        lastPanPos_ = event->pos();
        autoFit_ = false;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    horizontalRuler_->update();
    verticalRuler_->update();
    emit mouseMoved(true, mapToScene(event->pos()));
    QGraphicsView::mouseMoveEvent(event);
}

void TextPreview::leaveEvent(QEvent* event)
{
    mouseInside_ = false;
    horizontalRuler_->update();
    verticalRuler_->update();
    emit mouseMoved(false, QPointF());
    QGraphicsView::leaveEvent(event);
}

void TextPreview::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        viewport()->update();
    }
}

void TextPreview::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, palette().color(QPalette::AlternateBase));
}

void TextPreview::drawForeground(QPainter* painter, const QRectF&)
{
    if (!shape_.isEmpty() || message_.isEmpty()) {
        return;
    }
    painter->save();
    painter->resetTransform();
    painter->setPen(palette().color(QPalette::PlaceholderText));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 1.1);
    painter->setFont(f);
    painter->drawText(viewport()->rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, message_);
    painter->restore();
}

void TextPreview::layoutRulers()
{
    horizontalRuler_->setGeometry(kRulerThickness, 0, viewport()->width(), kRulerThickness);
    verticalRuler_->setGeometry(0, kRulerThickness, kRulerThickness, viewport()->height());
    updateRulers();
}

void TextPreview::updateRulers()
{
    horizontalRuler_->update();
    verticalRuler_->update();
}

}  // namespace ld::app
