#include "rulerwidget.h"

#include <QGraphicsView>
#include <QLocale>
#include <QPainter>
#include <cmath>

#include "core/rulerscale.h"
#include "appsettings.h"
#include "displayutil.h"

namespace ld::app {

RulerWidget::RulerWidget(Qt::Orientation orientation, QGraphicsView* view, const RulerSource* source)
    : QWidget(view), orientation_(orientation), view_(view), source_(source)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void RulerWidget::paintEvent(QPaintEvent*)
{
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Window));
        const QColor textColor = palette().color(QPalette::WindowText);
        const QColor tickColor = palette().color(QPalette::Mid);
        p.setPen(tickColor);

        const bool horizontal = orientation_ == Qt::Horizontal;
        const double ppm = source_->pixelsPerMm();
        const double unit = mmPerLengthUnit();   // 1 mm oder 25,4 mm (Zoll)
        const ld::core::RulerScale scale = ld::core::rulerScaleFor(ppm * unit);   // Teilung in der gewählten Einheit
        const double offset = horizontal ? source_->rulerOffsetMm().x() : source_->rulerOffsetMm().y();
        const double sign = horizontal ? source_->rulerSign().x() : source_->rulerSign().y();   // +1 oder -1

        // Sichtbarer Bereich in mm (Lineal-Koordinaten).
        const int length = horizontal ? width() : height();
        auto toMm = [&](int pixel) {
            const QPointF scenePoint = horizontal ? view_->mapToScene(QPoint(pixel, 0))
                                                  : view_->mapToScene(QPoint(0, pixel));
            return sign * (horizontal ? scenePoint.x() : scenePoint.y()) + offset;
        };
        const double minorMm = scale.minorMm() * unit;
        // Bei umgekehrter Zählrichtung ist der Wert am Anfang des Lineals der größere.
        const double lowMm = std::min(toMm(0), toMm(length));
        const double highMm = std::max(toMm(0), toMm(length));
        const long firstIndex = static_cast<long>(std::floor(lowMm / minorMm)) - 1;
        const long lastIndex = static_cast<long>(std::ceil(highMm / minorMm)) + 1;

        const int decimals = scale.majorMm >= 1.0 ? 0 : static_cast<int>(-std::floor(std::log10(scale.majorMm)));
        QFont small = font();
        small.setPointSizeF(font().pointSizeF() * 0.8);
        p.setFont(small);

        for (long i = firstIndex; i <= lastIndex; ++i) {
            const double mm = i * minorMm;
            const QPointF scenePoint = horizontal ? QPointF((mm - offset) / sign, 0) : QPointF(0, (mm - offset) / sign);
            const QPoint viewPoint = view_->mapFromScene(scenePoint);
            const int pos = horizontal ? viewPoint.x() : viewPoint.y();
            const bool major = (i % scale.subdivisions) == 0;
            const bool half = !major && scale.subdivisions % 2 == 0 && (i % (scale.subdivisions / 2)) == 0;
            const int tickLength = major ? kRulerThickness : (half ? kRulerThickness / 2 : kRulerThickness / 3);

            p.setPen(major ? textColor : tickColor);
            if (horizontal) {
                p.drawLine(pos, kRulerThickness - tickLength, pos, kRulerThickness);
            } else {
                p.drawLine(kRulerThickness - tickLength, pos, kRulerThickness, pos);
            }
            if (major) {
                QLocale locale;
                locale.setNumberOptions(QLocale::OmitGroupSeparator);
                const QString label = locale.toString(mm == 0.0 ? 0.0 : mm / unit, 'f', decimals);   // kein "-0"
                if (horizontal) {
                    p.drawText(pos + 3, 10, label);
                } else {
                    // Beschriftung gedreht, damit sie ins schmale Lineal passt.
                    p.save();
                    p.translate(10, pos - 3);
                    p.rotate(-90);
                    p.drawText(0, 0, label);
                    p.restore();
                }
            }
        }
        // Mausposition als Strich über die ganze Dicke des Lineals.
        if (source_->mouseInside()) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            if (horizontal) {
                p.drawLine(source_->mousePos().x(), 0, source_->mousePos().x(), kRulerThickness);
            } else {
                p.drawLine(0, source_->mousePos().y(), kRulerThickness, source_->mousePos().y());
            }
        }
        // Trennlinie zur Arbeitsfläche.
        p.setPen(tickColor);
        if (horizontal) {
            p.drawLine(0, height() - 1, width(), height() - 1);
        } else {
            p.drawLine(width() - 1, 0, width() - 1, height());
        }
}

}  // namespace ld::app
