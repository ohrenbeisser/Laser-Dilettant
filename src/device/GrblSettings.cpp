#include "device/GrblSettings.h"

#include <QRegularExpression>

bool GrblSettings::parseLine(const QString& line, int* number, QString* value)
{
    static const QRegularExpression re(QStringLiteral("^\\$(\\d+)\\s*=\\s*([^\\s(;]*)"));
    const auto m = re.match(line.trimmed());
    if (!m.hasMatch())
        return false;
    *number = m.captured(1).toInt();
    *value = m.captured(2);
    return true;
}

bool GrblSettings::parseAndStore(const QString& line)
{
    int n = 0;
    QString v;
    if (!parseLine(line, &n, &v))
        return false;
    m_values[n] = v;
    return true;
}

double GrblSettings::number(int n, double fallback) const
{
    if (!m_values.contains(n))
        return fallback;
    bool ok = false;
    const double v = m_values.value(n).toDouble(&ok);
    return ok ? v : fallback;
}

QString GrblSettings::toText(const QString& header) const
{
    QString out;
    if (!header.isEmpty()) {
        const QStringList lines = header.split(QLatin1Char('\n'));
        for (const QString& l : lines)
            out += QStringLiteral("; ") + l + QLatin1Char('\n');
    }
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        out += QStringLiteral("$%1=%2\n").arg(it.key()).arg(it.value());
    return out;
}

GrblSettings GrblSettings::fromText(const QString& text)
{
    GrblSettings s;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';')))
            continue;
        s.parseAndStore(t);
    }
    return s;
}
