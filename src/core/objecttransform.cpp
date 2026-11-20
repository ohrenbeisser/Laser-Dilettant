#include "core/objecttransform.h"

#include <cmath>
#include <numbers>

namespace ld::core {

ObjectTransform initialTransform(const QSize& imagePixels, double mmPerPixel)
{
    ObjectTransform t;
    if (imagePixels.width() <= 0 || imagePixels.height() <= 0 || mmPerPixel <= 0.0) {
        return t;  // alles 0
    }
    t.widthMm = imagePixels.width() * mmPerPixel;
    t.heightMm = imagePixels.height() * mmPerPixel;
    return t;
}

ObjectTransform withWidth(const ObjectTransform& t, double newWidthMm, bool keepAspect)
{
    if (newWidthMm <= 0.0) {
        return t;
    }
    ObjectTransform result = t;
    if (keepAspect && t.widthMm > 0.0 && t.heightMm > 0.0) {
        result.heightMm = t.heightMm * (newWidthMm / t.widthMm);
    }
    result.widthMm = newWidthMm;
    return result;
}

ObjectTransform withHeight(const ObjectTransform& t, double newHeightMm, bool keepAspect)
{
    if (newHeightMm <= 0.0) {
        return t;
    }
    ObjectTransform result = t;
    if (keepAspect && t.widthMm > 0.0 && t.heightMm > 0.0) {
        result.widthMm = t.widthMm * (newHeightMm / t.heightMm);
    }
    result.heightMm = newHeightMm;
    return result;
}

QSizeF boundingSize(const ObjectTransform& t)
{
    // Ein Rechteck der Größe w x h, um den Winkel a gedreht, hat das umschließende Rechteck
    //   w' = w*|cos a| + h*|sin a|     h' = w*|sin a| + h*|cos a|
    const double radians = t.rotationDeg * std::numbers::pi / 180.0;
    const double c = std::abs(std::cos(radians));
    const double s = std::abs(std::sin(radians));
    return QSizeF(t.widthMm * c + t.heightMm * s, t.widthMm * s + t.heightMm * c);
}

}  // namespace ld::core
