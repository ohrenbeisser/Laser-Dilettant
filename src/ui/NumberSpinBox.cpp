#include "ui/NumberSpinBox.h"

#include <QLocale>

NumberSpinBox::NumberSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    setKeyboardTracking(false);
}

NumberSpinBox::NumberSpinBox(double min, double max, int decimals, const QString& suffix, QWidget* parent)
    : NumberSpinBox(parent)
{
    setDecimals(decimals);
    setRange(min, max);
    if (!suffix.isEmpty())
        setSuffix(QLatin1Char(' ') + suffix);
}

QString NumberSpinBox::normalized(const QString& text) const
{
    const QString dp = locale().decimalPoint();
    QString t = text;
    t.replace(QLatin1Char(','), dp);
    t.replace(QLatin1Char('.'), dp);
    return t;
}

QValidator::State NumberSpinBox::validate(QString& input, int& pos) const
{
    QString t = normalized(input);
    const QValidator::State s = QDoubleSpinBox::validate(t, pos);
    if (s != QValidator::Invalid)
        input = t;
    return s;
}

double NumberSpinBox::valueFromText(const QString& text) const
{
    return QDoubleSpinBox::valueFromText(normalized(text));
}

void NumberSpinBox::fixup(QString& input) const
{
    input = normalized(input);
    QDoubleSpinBox::fixup(input);
}
