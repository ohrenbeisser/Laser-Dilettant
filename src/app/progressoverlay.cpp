#include "progressoverlay.h"

#include <QPainter>
#include <algorithm>

namespace ld::app {

void ProgressOverlay::reset(const QRectF& sceneRect, double lineWidthMm)
{
    rect_ = sceneRect;
    lineWidthMm_ = lineWidthMm;
    const double longSide = std::max(sceneRect.width(), sceneRect.height());
    if (longSide <= 0.0) {
        clear();
        return;
    }
    scale_ = std::min(2048.0 / longSide, 40.0);
    image_ = QImage(static_cast<int>(std::ceil(sceneRect.width() * scale_)) + 1,
                    static_cast<int>(std::ceil(sceneRect.height() * scale_)) + 1,
                    QImage::Format_ARGB32_Premultiplied);
    image_.fill(Qt::transparent);
}

void ProgressOverlay::clear()
{
    image_ = QImage();
}

void ProgressOverlay::paint(const ld::core::PathData& data, size_t first, size_t last)
{
    if (image_.isNull() || first >= last) {
        return;
    }
    last = std::min(last, data.segments.size());
    QPainter painter(&image_);
    painter.scale(scale_, scale_);
    painter.translate(-rect_.left(), -rect_.top());
    // Standardgrün aus dem Mockup; die Deckkraft kommt erst beim Zeichnen auf die Arbeitsfläche,
    // damit sich überlappende Linien nicht aufaddieren.
    QPen pen(color_, std::max(lineWidthMm_, 1.0 / scale_));
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    for (size_t i = first; i < last; ++i) {
        const ld::core::PathSegment& s = data.segments[i];
        if (s.kind == ld::core::SegmentKind::Burn) {
            painter.drawLine(QLineF(s.x0, s.y0, s.x1, s.y1));
        }
    }
}

}  // namespace ld::app
