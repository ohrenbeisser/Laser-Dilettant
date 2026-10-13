#pragma once

#include <QDoubleSpinBox>

// QDoubleSpinBox that accepts both ',' and '.' as decimal separator,
// independent of the language.
class NumberSpinBox : public QDoubleSpinBox
{
    Q_OBJECT
public:
    explicit NumberSpinBox(QWidget* parent = nullptr);
    NumberSpinBox(double min, double max, int decimals, const QString& suffix = QString(), QWidget* parent = nullptr);

protected:
    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;
    void fixup(QString& input) const override;

private:
    QString normalized(const QString& text) const;
};
