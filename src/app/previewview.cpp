#include "previewview.h"

#include <QGraphicsPixmapItem>

#include "appsettings.h"
#include "displayutil.h"
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace ld::app {

namespace {
constexpr double kHandleTolerance = 8.0;   // Bildschirmpixel
constexpr double kMinCropSize = 4.0;       // Pixel des Originals
}  // namespace

PreviewView::PreviewView(QWidget* parent) : QGraphicsView(parent)
{
    auto* scene = new QGraphicsScene(this);
    setScene(scene);
    resultItem_ = scene->addPixmap(QPixmap());
    sourceItem_ = scene->addPixmap(QPixmap());
    sourceItem_->setTransformationMode(Qt::SmoothTransformation);
    sourceItem_->setVisible(false);

    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::StyledPanel);
    setMouseTracking(true);
    setMinimumSize(360, 300);
}

QRectF PreviewView::currentBounds() const
{
    if (cropMode_) {
        return QRectF(QPointF(0, 0), QSizeF(sourceSize_));
    }
    return QRectF(QPointF(0, 0), resultSizeMm_);
}

void PreviewView::setResult(const QImage& raster, const QSizeF& sizeMm, bool fitToView)
{
    if (raster.isNull()) {
        return;
    }
    const DisplayColors& colors = AppSettings::instance().colors();
    resultItem_->setPixmap(QPixmap::fromImage(recolorRaster(raster, colors.imageBurn, colors.imagePaper)));
    resultPixelWidth_ = raster.width();
    resultSizeMm_ = sizeMm;
    // Szene in mm: ein Rasterpixel ist sizeMm / Pixelzahl groß.
    resultItem_->setTransform(
        QTransform::fromScale(sizeMm.width() / raster.width(), sizeMm.height() / raster.height()));
    if (!cropMode_) {
        const QRectF bounds = currentBounds();
        scene()->setSceneRect(bounds.adjusted(-bounds.width(), -bounds.height(), bounds.width(), bounds.height()));
        if (fitToView) {
            fit();
        }
        updateSampling();
        viewport()->update();
    }
}

void PreviewView::setSourceImage(const QImage& image)
{
    sourceSize_ = image.size();
    sourceItem_->setTransform(QTransform());
    sourceItem_->setPixmap(QPixmap::fromImage(image));
}

void PreviewView::setCropBackdrop(const QImage& raster, const QSize& sourceSize)
{
    if (raster.isNull() || sourceSize.isEmpty()) {
        return;
    }
    const DisplayColors& colors = AppSettings::instance().colors();
    sourceSize_ = sourceSize;
    sourceItem_->setPixmap(QPixmap::fromImage(recolorRaster(raster, colors.imageBurn, colors.imagePaper)));
    sourceItem_->setTransform(QTransform::fromScale(static_cast<double>(sourceSize.width()) / raster.width(),
                                                    static_cast<double>(sourceSize.height()) / raster.height()));
    viewport()->update();
}

void PreviewView::setCropMode(bool on, const QRect& initialCrop)
{
    cropMode_ = on;
    resultItem_->setVisible(!on);
    sourceItem_->setVisible(on);
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    const QRectF bounds = currentBounds();
    scene()->setSceneRect(bounds.adjusted(-bounds.width(), -bounds.height(), bounds.width(), bounds.height()));
    if (on) {
        const QRect full(QPoint(0, 0), sourceSize_);
        setCrop(initialCrop.isEmpty() ? QRectF(full) : QRectF(initialCrop.intersected(full)));
        viewport()->setCursor(Qt::CrossCursor);
    } else {
        viewport()->unsetCursor();
    }
    fit();
    updateSampling();
    viewport()->update();
}

void PreviewView::setCropRect(const QRect& rect)
{
    setCrop(QRectF(rect));
}

void PreviewView::setCrop(const QRectF& rect)
{
    cropRect_ = rect;
    emit cropChanged(cropRect_.toRect());
    viewport()->update();
}

void PreviewView::fit()
{
    const QRectF bounds = currentBounds();
    if (bounds.isEmpty()) {
        return;
    }
    fitInView(bounds.adjusted(-bounds.width() * 0.03, -bounds.height() * 0.03, bounds.width() * 0.03,
                              bounds.height() * 0.03),
              Qt::KeepAspectRatio);
    updateSampling();
    emit zoomChanged();
}

void PreviewView::showOneToOne()
{
    resetTransform();
    if (cropMode_) {
        // ein Pixel des Originals pro Bildschirmpixel
    } else if (resultSizeMm_.width() > 0 && resultPixelWidth_ > 0) {
        const double z = resultPixelWidth_ / resultSizeMm_.width();  // Bildschirmpixel pro mm
        scale(z, z);
    }
    updateSampling();
    emit zoomChanged();
}

void PreviewView::zoomBy(double factor)
{
    const double newPercent = zoomPercent() * factor;
    if (newPercent < 5.0 || newPercent > 4000.0) {
        return;
    }
    // Ausnahmsweise um die Mitte des Ausschnitts zoomen (sonst gilt die Mausposition).
    const auto previousAnchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(previousAnchor);
    updateSampling();
    emit zoomChanged();
}

double PreviewView::zoomPercent() const
{
    const double screenPerUnit = transform().m11();
    if (cropMode_ || resultSizeMm_.width() <= 0 || resultPixelWidth_ <= 0) {
        return screenPerUnit * 100.0;
    }
    return screenPerUnit * (resultSizeMm_.width() / resultPixelWidth_) * 100.0;
}

void PreviewView::updateSampling()
{
    // Ab 1:1 die Pixel scharf zeigen (Dithering beurteilt man so), kleiner geglättet.
    const bool sharp = zoomPercent() >= 100.0;
    resultItem_->setTransformationMode(sharp ? Qt::FastTransformation : Qt::SmoothTransformation);
    setRenderHint(QPainter::SmoothPixmapTransform, !sharp);
}

void PreviewView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        return;
    }
    const double factor = std::pow(1.25, steps);
    const double newPercent = zoomPercent() * factor;
    if (newPercent < 5.0 || newPercent > 4000.0) {
        return;
    }
    scale(factor, factor);
    updateSampling();
    emit zoomChanged();
    event->accept();
}

void PreviewView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    emit zoomChanged();
}

void PreviewView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, palette().color(QPalette::AlternateBase));
}

// ------------------------------------------------------------------ Zuschneiden: Maus
int PreviewView::edgesAt(const QPoint& viewPos) const
{
    const QRect r = mapFromScene(cropRect_).boundingRect();
    const bool inX = viewPos.x() >= r.left() - kHandleTolerance && viewPos.x() <= r.right() + kHandleTolerance;
    const bool inY = viewPos.y() >= r.top() - kHandleTolerance && viewPos.y() <= r.bottom() + kHandleTolerance;
    int edges = None;
    if (inY && std::abs(viewPos.x() - r.left()) <= kHandleTolerance) edges |= Left;
    if (inY && std::abs(viewPos.x() - r.right()) <= kHandleTolerance) edges |= Right;
    if (inX && std::abs(viewPos.y() - r.top()) <= kHandleTolerance) edges |= Top;
    if (inX && std::abs(viewPos.y() - r.bottom()) <= kHandleTolerance) edges |= Bottom;
    return edges;
}

void PreviewView::updateCursorFor(const QPoint& viewPos)
{
    const int edges = edgesAt(viewPos);
    Qt::CursorShape shape = Qt::CrossCursor;
    if (edges == (Left | Top) || edges == (Right | Bottom)) {
        shape = Qt::SizeFDiagCursor;
    } else if (edges == (Right | Top) || edges == (Left | Bottom)) {
        shape = Qt::SizeBDiagCursor;
    } else if (edges & (Left | Right)) {
        shape = Qt::SizeHorCursor;
    } else if (edges & (Top | Bottom)) {
        shape = Qt::SizeVerCursor;
    } else if (mapFromScene(cropRect_).boundingRect().contains(viewPos)) {
        shape = Qt::SizeAllCursor;
    }
    viewport()->setCursor(shape);
}

QPointF PreviewView::clampedScenePoint(const QPoint& viewPos) const
{
    const QRectF bounds = currentBounds();
    const QPointF p = mapToScene(viewPos);
    return QPointF(std::clamp(std::round(p.x()), bounds.left(), bounds.right()),
                   std::clamp(std::round(p.y()), bounds.top(), bounds.bottom()));
}

void PreviewView::mousePressEvent(QMouseEvent* event)
{
    if (!cropMode_ || event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    dragStart_ = clampedScenePoint(event->pos());
    dragOriginalRect_ = cropRect_;
    dragEdges_ = edgesAt(event->pos());
    if (dragEdges_ != None) {
        drag_ = Drag::Resize;
    } else if (mapFromScene(cropRect_).boundingRect().contains(event->pos())) {
        drag_ = Drag::Move;
    } else {
        drag_ = Drag::NewRect;
    }
    event->accept();
}

void PreviewView::mouseMoveEvent(QMouseEvent* event)
{
    if (!cropMode_) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    if (drag_ == Drag::None) {
        updateCursorFor(event->pos());
        return;
    }
    const QRectF bounds = currentBounds();
    const QPointF p = clampedScenePoint(event->pos());

    if (drag_ == Drag::NewRect) {
        QRectF r = QRectF(dragStart_, p).normalized();
        if (r.width() < kMinCropSize) r.setWidth(kMinCropSize);
        if (r.height() < kMinCropSize) r.setHeight(kMinCropSize);
        setCrop(r.intersected(bounds));
    } else if (drag_ == Drag::Move) {
        QRectF r = dragOriginalRect_.translated(p - dragStart_);
        // Im Bild halten, Größe bleibt.
        r.moveLeft(std::clamp(r.left(), bounds.left(), bounds.right() - r.width()));
        r.moveTop(std::clamp(r.top(), bounds.top(), bounds.bottom() - r.height()));
        setCrop(r);
    } else {
        QRectF r = dragOriginalRect_;
        if (dragEdges_ & Left) r.setLeft(std::min(p.x(), r.right() - kMinCropSize));
        if (dragEdges_ & Right) r.setRight(std::max(p.x(), r.left() + kMinCropSize));
        if (dragEdges_ & Top) r.setTop(std::min(p.y(), r.bottom() - kMinCropSize));
        if (dragEdges_ & Bottom) r.setBottom(std::max(p.y(), r.top() + kMinCropSize));
        setCrop(r.intersected(bounds));
    }
    event->accept();
}

void PreviewView::mouseReleaseEvent(QMouseEvent* event)
{
    if (cropMode_ && drag_ != Drag::None) {
        drag_ = Drag::None;
        updateCursorFor(event->pos());
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

// ------------------------------------------------------------------ Zuschneiden: Zeichnen
void PreviewView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (!cropMode_) {
        return;
    }
    // Außerhalb des Rahmens abdunkeln.
    QPainterPath outside;
    outside.addRect(currentBounds());
    QPainterPath inside;
    inside.addRect(cropRect_);
    painter->fillPath(outside.subtracted(inside), QColor(0, 0, 0, 130));

    // Rahmen und Griffe in Bildschirmkoordinaten zeichnen.
    painter->save();
    painter->resetTransform();
    const QRect r = mapFromScene(cropRect_).boundingRect();
    painter->setPen(QPen(Qt::white, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r);
    painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
    painter->drawRect(r.adjusted(1, 1, -1, -1));
    painter->setPen(Qt::black);
    painter->setBrush(Qt::white);
    const QPoint centers[] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(),
                              QPoint(r.center().x(), r.top()), QPoint(r.center().x(), r.bottom()),
                              QPoint(r.left(), r.center().y()), QPoint(r.right(), r.center().y())};
    for (const QPoint& c : centers) {
        painter->drawRect(QRect(c.x() - 4, c.y() - 4, 8, 8));
    }
    painter->restore();
}

}  // namespace ld::app
