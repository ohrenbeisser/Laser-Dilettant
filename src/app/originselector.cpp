#include "originselector.h"

#include <QMouseEvent>
#include <QPainter>

namespace ld::app {

OriginSelector::OriginSelector(QWidget* parent) : QWidget(parent)
{
    setFixedSize(66, 54);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Job-Ursprung: Dieser Punkt des Bildes liegt auf dem Nullpunkt. "
                  "Von hier aus zählen auch die Lineale und die Maße."));
}

void OriginSelector::setOrigin(ld::core::JobOrigin origin)
{
    origin_ = ld::core::normalizedJobOrigin(origin);
    update();
}

bool OriginSelector::isSelectable(int index) const
{
    return ld::core::isSelectableJobOrigin(static_cast<ld::core::JobOrigin>(index));
}

QPointF OriginSelector::pointFor(int index) const
{
    const QRectF frame = QRectF(rect()).adjusted(6, 6, -6, -6);
    return QPointF(frame.left() + frame.width() * (index % 3) / 2.0, frame.top() + frame.height() * (index / 3) / 2.0);
}

void OriginSelector::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(6, 6, -6, -6);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);   // das Bild
    for (int i = 0; i < 9; ++i) {
        if (!isSelectable(i)) continue;   // nur die fünf Punkte wie auf dem Würfel
        const QPointF c = pointFor(i);
        const bool selected = i == static_cast<int>(origin_);
        p.setPen(Qt::NoPen);
        p.setBrush(selected ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid));
        p.drawEllipse(c, selected ? 5.0 : 3.0, selected ? 5.0 : 3.0);
    }
}

void OriginSelector::mousePressEvent(QMouseEvent* event)
{
    int best = 0;
    double bestDistance = 1e9;
    for (int i = 0; i < 9; ++i) {
        if (!isSelectable(i)) continue;
        const QPointF d = pointFor(i) - event->position();
        const double distance = d.x() * d.x() + d.y() * d.y();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    const auto chosen = static_cast<ld::core::JobOrigin>(best);
    if (chosen != origin_) {
        origin_ = chosen;
        update();
        emit originChanged(chosen);
    }
}

}  // namespace ld::app
