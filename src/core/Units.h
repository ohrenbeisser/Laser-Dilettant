#pragma once

#include <QString>

// Helpers for unit conversion and number formatting.
//
// G-code numbers always use '.' as decimal separator. Numbers shown to the
// user follow the current QLocale. User input accepts both ',' and '.'.
namespace Units {

// Formats a number for G-code: at most maxDecimals decimals, trailing zeros
// and a trailing '.' removed, "-0" normalised to "0".
QString gcodeNumber(double value, int maxDecimals = 3);

// Formats a number for display with exactly `decimals` decimals in the
// current locale (decimal comma in German).
QString display(double value, int decimals);

// Parses user input; accepts ',' and '.' as decimal separator and ignores
// surrounding whitespace. Thousands separators are not accepted.
double parse(const QString& text, bool* ok = nullptr);

constexpr double mmPerSecToMmPerMin(double mmPerSec) { return mmPerSec * 60.0; }
constexpr double mmPerMinToMmPerSec(double mmPerMin) { return mmPerMin / 60.0; }

// Converts laser power in percent (0..100) to a GRBL S value.
constexpr double percentToS(double percent, double maxS) { return percent / 100.0 * maxS; }
constexpr double sToPercent(double s, double maxS) { return maxS > 0 ? s / maxS * 100.0 : 0.0; }

// Formats a duration in seconds as h:mm:ss.
QString duration(double seconds);

} // namespace Units
