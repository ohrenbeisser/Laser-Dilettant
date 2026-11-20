#include "core/units.h"

namespace ld::core {

double mmPerPixelFromDpi(double dpi)
{
    if (dpi <= 0.0) {
        return 0.0;  // Ungültig: Der Aufrufer prüft auf 0.
    }
    return kMmPerInch / dpi;
}

double dpiFromMmPerPixel(double mmPerPixel)
{
    if (mmPerPixel <= 0.0) {
        return 0.0;
    }
    return kMmPerInch / mmPerPixel;
}

double sizeInMm(int pixelCount, double mmPerPixel)
{
    if (pixelCount < 0 || mmPerPixel <= 0.0) {
        return 0.0;
    }
    return pixelCount * mmPerPixel;
}

}  // namespace ld::core
