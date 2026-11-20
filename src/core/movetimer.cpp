#include "core/movetimer.h"

#include <algorithm>
#include <cmath>

namespace ld::core {

MoveTimer::MoveTimer(double rapidMmPerMin, double accelX, double accelY)
    : rapidMmPerSecond_(rapidMmPerMin / 60.0), accelX_(accelX), accelY_(accelY)
{
}

void MoveTimer::add(const QPointF& from, const QPointF& to, bool rapid, double feedMmPerMin)
{
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double length = std::hypot(dx, dy);
    if (length <= 0.0) {
        return;
    }
    const double speed = (rapid || feedMmPerMin <= 0.0) ? rapidMmPerSecond_ : feedMmPerMin / 60.0;
    simple_ += length / speed;

    const double ux = dx / length;
    const double uy = dy / length;
    // Gleiche Richtung und gleicher Vorschub: dieselbe Fahrt.
    if (open_ && std::abs(speed - speed_) < 1e-9 && ux * dirX_ + uy * dirY_ > 0.99999) {
        length_ += length;
        return;
    }
    flush();
    open_ = true;
    dirX_ = ux;
    dirY_ = uy;
    speed_ = speed;
    length_ = length;
}

void MoveTimer::flush()
{
    if (!open_) {
        return;
    }
    open_ = false;
    double a = 0.0;   // wirksame Beschleunigung entlang der Fahrtrichtung
    if (accelX_ > 0.0 && accelY_ > 0.0) {
        // Jede Achse darf höchstens ihre Beschleunigung: entlang der Richtung gilt die engere Grenze.
        const double ax = std::abs(dirX_) > 1e-9 ? accelX_ / std::abs(dirX_) : 1e300;
        const double ay = std::abs(dirY_) > 1e-9 ? accelY_ / std::abs(dirY_) : 1e300;
        a = std::min(ax, ay);
    }
    if (a <= 0.0) {
        total_ += length_ / speed_;
        return;
    }
    const double rampLength = speed_ * speed_ / a;   // Beschleunigen plus Bremsen
    if (length_ >= rampLength) {
        total_ += length_ / speed_ + speed_ / a;
    } else {
        total_ += 2.0 * std::sqrt(length_ / a);       // Dreieck: Vorschub wird nie erreicht
    }
}

double MoveTimer::seconds()
{
    flush();
    return total_;
}

}  // namespace ld::core
