#include "gcode/GcodeNormalizer.h"

namespace GcodeNormalizer {

Result normalize(const QString& line, QString* out)
{
    QString r;
    r.reserve(line.size());
    bool inParen = false;
    for (const QChar c : line) {
        if (inParen) {
            if (c == QLatin1Char(')'))
                inParen = false;
            continue;
        }
        if (c == QLatin1Char('('))
            inParen = true;
        else if (c == QLatin1Char(';'))
            break;
        else if (!c.isSpace())
            r.append(c.toUpper());
    }
    // '%' marks program start/end in some files and has no meaning for GRBL.
    if (r == QLatin1String("%"))
        r.clear();
    if (out)
        *out = r;
    if (r.isEmpty())
        return Result::Empty;
    if (r.size() > kMaxLineLength)
        return Result::TooLong;
    return Result::Ok;
}

bool normalizeAll(const QStringList& in, QStringList* out, int* badLine)
{
    out->clear();
    out->reserve(in.size());
    QString n;
    for (int i = 0; i < in.size(); ++i) {
        switch (normalize(in[i], &n)) {
        case Result::Ok:
            out->append(n);
            break;
        case Result::Empty:
            break;
        case Result::TooLong:
            if (badLine)
                *badLine = i + 1;
            return false;
        }
    }
    return true;
}

} // namespace GcodeNormalizer
