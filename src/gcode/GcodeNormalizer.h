#pragma once

#include <QString>
#include <QStringList>

namespace GcodeNormalizer {

// GRBL's line buffer holds 80 characters (without line end).
constexpr int kMaxLineLength = 80;

enum class Result { Ok, Empty, TooLong };

// Removes comments "( ... )" and "; ...", all whitespace and converts to
// upper case. Returns Empty for lines that contain nothing to send and
// TooLong if the result exceeds kMaxLineLength.
Result normalize(const QString& line, QString* out);

// Normalises a whole file. Empty lines are dropped. On the first line that is
// too long, returns false and reports its 1-based number in *badLine.
bool normalizeAll(const QStringList& in, QStringList* out, int* badLine = nullptr);

} // namespace GcodeNormalizer
