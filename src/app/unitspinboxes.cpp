#include "unitspinboxes.h"

#include <QCoreApplication>

#include <QLocale>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

#include "appsettings.h"

namespace ld::app {

LengthSpinBox::LengthSpinBox(double minMm, double maxMm, int decimalsMm, int decimalsInch, double stepMm,
                             QWidget* parent)
    : QDoubleSpinBox(parent), minMm_(minMm), maxMm_(maxMm), stepMm_(stepMm), decimalsMm_(decimalsMm),
      decimalsInch_(decimalsInch), mm_(minMm)
{
    setKeyboardTracking(false);   // erst bei Enter, Fokuswechsel oder Pfeil übernehmen
    connect(this, &QDoubleSpinBox::valueChanged, this, [this](double shown) {
        mm_ = std::clamp(displayToMm(shown), minMm_, maxMm_);
        emit mmChanged(mm_);
    });
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, &LengthSpinBox::applyUnit);
    applyUnit();
}

void LengthSpinBox::applyUnit()
{
    const QSignalBlocker blocker(this);
    const bool inch = AppSettings::instance().lengthUnit() == LengthUnit::Inch;
    setRange(mmToDisplay(minMm_), mmToDisplay(maxMm_));
    setDecimals(inch ? decimalsInch_ : decimalsMm_);
    setSingleStep(mmToDisplay(stepMm_));
    setSuffix(lengthSuffix());
    setValue(mmToDisplay(mm_));   // der echte Wert in mm bleibt erhalten
}

void LengthSpinBox::setMm(double mm)
{
    mm_ = std::clamp(mm, minMm_, maxMm_);
    const QSignalBlocker blocker(this);
    setValue(mmToDisplay(mm_));
}

ResolutionSpinBox::ResolutionSpinBox(double minMm, double maxMm, QWidget* parent)
    : QDoubleSpinBox(parent), minMm_(minMm), maxMm_(maxMm), mm_(minMm)
{
    setKeyboardTracking(false);
    connect(this, &QDoubleSpinBox::valueChanged, this, [this](double shown) {
        mm_ = std::clamp(fromDisplay(shown), minMm_, maxMm_);
        emit mmChanged(mm_);
    });
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, &ResolutionSpinBox::applyUnit);
    applyUnit();
}

double ResolutionSpinBox::toDisplay(double mm) const
{
    switch (AppSettings::instance().resolutionUnit()) {
    case ResolutionUnit::Dpi: return 25.4 / mm;
    case ResolutionUnit::PixelSize: return mmToDisplay(mm);
    case ResolutionUnit::LinesPerMm: break;
    }
    return 1.0 / mm;
}

double ResolutionSpinBox::fromDisplay(double shown) const
{
    if (shown <= 0.0) return maxMm_;
    switch (AppSettings::instance().resolutionUnit()) {
    case ResolutionUnit::Dpi: return 25.4 / shown;
    case ResolutionUnit::PixelSize: return displayToMm(shown);
    case ResolutionUnit::LinesPerMm: break;
    }
    return 1.0 / shown;
}

void ResolutionSpinBox::applyUnit()
{
    const QSignalBlocker blocker(this);
    const ResolutionUnit unit = AppSettings::instance().resolutionUnit();
    const bool inch = AppSettings::instance().lengthUnit() == LengthUnit::Inch;
    // Linien/mm und dpi laufen umgekehrt zur Pixelgröße: kleinste Pixelgröße = größter Wert.
    const double a = toDisplay(minMm_), b = toDisplay(maxMm_);
    setRange(std::min(a, b), std::max(a, b));
    switch (unit) {
    case ResolutionUnit::LinesPerMm: setDecimals(1); setSingleStep(0.5); break;
    case ResolutionUnit::Dpi: setDecimals(0); setSingleStep(10.0); break;
    case ResolutionUnit::PixelSize: setDecimals(inch ? 5 : 4); setSingleStep(mmToDisplay(0.005)); break;
    }
    setValue(toDisplay(mm_));   // der echte Wert in mm bleibt erhalten
}

void ResolutionSpinBox::setMm(double mm)
{
    mm_ = std::clamp(mm, minMm_, maxMm_);
    const QSignalBlocker blocker(this);
    setValue(toDisplay(mm_));
}

QString ResolutionSpinBox::textFromValue(double value) const
{
    const QLocale locale;
    switch (AppSettings::instance().resolutionUnit()) {
    case ResolutionUnit::Dpi:
        return locale.toString(value, 'f', 0) + QStringLiteral(" dpi");
    case ResolutionUnit::PixelSize:
        return locale.toString(value, 'f', decimals()) + lengthSuffix();
    case ResolutionUnit::LinesPerMm: break;
    }
    // Linien/mm mit den dpi in Klammern (dpi = Linien/mm * 25,4). Die dpi kommen vom echten Wert, nicht vom auf eine
    // Stelle gerundeten (15,7 Linien/mm wären sonst 399 statt 400 dpi).
    double dpiValue = value * 25.4;
    if (mm_ > 0.0 && std::abs(std::round(10.0 / mm_) / 10.0 - value) < 1e-9) dpiValue = 25.4 / mm_;
    return QCoreApplication::translate("ld::app", "%1 Linien/mm (%2 dpi)")
        .arg(locale.toString(value, 'f', 1), locale.toString(dpiValue, 'f', 0));
}

double ResolutionSpinBox::valueFromText(const QString& text) const
{
    // Die erste Zahl im Text zählt; Einheit und Klammer dahinter werden ignoriert.
    static const QRegularExpression number(QStringLiteral("[0-9]+(?:[.,][0-9]+)?"));
    const QRegularExpressionMatch m = number.match(text);
    if (!m.hasMatch()) return value();
    QString digits = m.captured(0);
    digits.replace(QLatin1Char(','), QLatin1Char('.'));
    return digits.toDouble();
}

QValidator::State ResolutionSpinBox::validate(QString& input, int& /*pos*/) const
{
    // Beliebiger Text um die Zahl herum ist erlaubt (Einheit, Klammer); ohne Ziffer ist die Eingabe unfertig.
    static const QRegularExpression digit(QStringLiteral("[0-9]"));
    return input.contains(digit) ? QValidator::Acceptable : QValidator::Intermediate;
}

SpeedSpinBox::SpeedSpinBox(double minMmPerMin, double maxMmPerMin, QWidget* parent)
    : QDoubleSpinBox(parent), minV_(minMmPerMin), maxV_(maxMmPerMin), value_(minMmPerMin)
{
    setKeyboardTracking(false);
    connect(this, &QDoubleSpinBox::valueChanged, this, [this](double shown) {
        value_ = std::clamp(displayToSpeed(shown), minV_, maxV_);
        emit mmPerMinChanged(value_);
    });
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, &SpeedSpinBox::applyUnit);
    applyUnit();
}

void SpeedSpinBox::applyUnit()
{
    const QSignalBlocker blocker(this);
    const bool perSecond = AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec;
    setRange(speedToDisplay(minV_), speedToDisplay(maxV_));
    setDecimals(perSecond ? 1 : 0);
    setSingleStep(perSecond ? 1.0 : 100.0);
    setSuffix(speedSuffix());
    setValue(speedToDisplay(value_));
}

void SpeedSpinBox::setMaxMmPerMin(double maxMmPerMin)
{
    maxV_ = std::max(minV_ + 1.0, maxMmPerMin);
    value_ = std::min(value_, maxV_);
    applyUnit();
}

void SpeedSpinBox::setMmPerMin(double v)
{
    value_ = std::clamp(v, minV_, maxV_);
    const QSignalBlocker blocker(this);
    setValue(speedToDisplay(value_));
}

}  // namespace ld::app
