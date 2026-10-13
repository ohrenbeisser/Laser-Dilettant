#include "core/Units.h"

#include <QLocale>

#include <cmath>

namespace Units {

QString gcodeNumber(double value, int maxDecimals)
{
    const double scale = std::pow(10.0, maxDecimals);
    double rounded = std::round(value * scale) / scale;
    if (rounded == 0.0)
        rounded = 0.0; // turns -0 into 0
    QString s = QString::number(rounded, 'f', maxDecimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

QString display(double value, int decimals)
{
    return QLocale().toString(value, 'f', decimals);
}

double parse(const QString& text, bool* ok)
{
    QString s = text.trimmed();
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    bool good = false;
    const double v = QLocale::c().toDouble(s, &good);
    if (ok)
        *ok = good;
    return good ? v : 0.0;
}

QString duration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0)
        seconds = 0;
    const qint64 total = static_cast<qint64>(std::llround(seconds));
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace Units
