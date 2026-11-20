#include "core/rulerscale.h"

#include <cmath>

namespace ld::core {

RulerScale rulerScaleFor(double pixelsPerMm, double minMajorPixels)
{
    if (pixelsPerMm <= 0.0 || minMajorPixels <= 0.0) {
        return {};
    }
    // Kleinster zulässiger Abstand in mm, dann auf 1/2/5 * 10^n aufrunden.
    const double minMm = minMajorPixels / pixelsPerMm;
    const double base = std::pow(10.0, std::floor(std::log10(minMm)));
    const double mantissa = minMm / base;  // in [1, 10)

    RulerScale scale;
    if (mantissa <= 1.0) {
        scale.majorMm = base;
        scale.subdivisions = 10;
    } else if (mantissa <= 2.0) {
        scale.majorMm = 2.0 * base;
        scale.subdivisions = 4;
    } else if (mantissa <= 5.0) {
        scale.majorMm = 5.0 * base;
        scale.subdivisions = 5;
    } else {
        scale.majorMm = 10.0 * base;
        scale.subdivisions = 10;
    }
    return scale;
}

}  // namespace ld::core
