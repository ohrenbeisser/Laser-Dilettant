#include "core/machinesetup.h"

#include <algorithm>
#include <cmath>

#include "core/engraving.h"

namespace ld::core {

JobOrigin normalizedJobOrigin(JobOrigin origin)
{
    switch (origin) {
    case JobOrigin::TopLeft:
    case JobOrigin::TopRight:
    case JobOrigin::Center:
    case JobOrigin::BottomLeft:
    case JobOrigin::BottomRight:
        return origin;
    default:
        return JobOrigin::Center;
    }
}

QPointF jobOriginRulerSign(JobOrigin origin)
{
    const QPointF fraction = jobOriginFraction(normalizedJobOrigin(origin));
    return QPointF(fraction.x() >= 1.0 ? -1.0 : 1.0, fraction.y() <= 0.0 ? 1.0 : -1.0);
}

QPointF jobOriginFraction(JobOrigin origin)
{
    switch (origin) {
    case JobOrigin::TopLeft:      return {0.0, 0.0};
    case JobOrigin::TopCenter:    return {0.5, 0.0};
    case JobOrigin::TopRight:     return {1.0, 0.0};
    case JobOrigin::MiddleLeft:   return {0.0, 0.5};
    case JobOrigin::Center:       return {0.5, 0.5};
    case JobOrigin::MiddleRight:  return {1.0, 0.5};
    case JobOrigin::BottomLeft:   return {0.0, 1.0};
    case JobOrigin::BottomCenter: return {0.5, 1.0};
    case JobOrigin::BottomRight:  return {1.0, 1.0};
    }
    return {0.0, 0.0};
}

namespace {
// Drehung um ein Vielfaches von 90 Grad gegen den Uhrzeigersinn (Y nach oben), exakt ohne
// Rundungsfehler von sin/cos.
QPointF rotateCcw(const QPointF& p, int degrees)
{
    switch (((degrees % 360) + 360) % 360) {
    case 90:  return {-p.y(), p.x()};
    case 180: return {-p.x(), -p.y()};
    case 270: return {p.y(), -p.x()};
    default:  return p;
    }
}
}  // namespace

QPointF operatorToMachine(const MachineSetup& setup, const QPointF& operatorUp)
{
    QPointF p = operatorUp;
    if (setup.mirrorX) {
        p.setX(-p.x());
    }
    // Ist der Laser um R im Uhrzeigersinn gedreht, erscheinen seine Achsen für den Bediener
    // um R im Uhrzeigersinn gedreht; ein Punkt des Bedieners hat in der Maschine daher Koordinaten,
    // die um R GEGEN den Uhrzeigersinn gedreht sind.
    return rotateCcw(p, setup.rotationDeg);
}

QPointF machineToOperator(const MachineSetup& setup, const QPointF& machine)
{
    QPointF p = rotateCcw(machine, 360 - (((setup.rotationDeg % 360) + 360) % 360));
    if (setup.mirrorX) {
        p.setX(-p.x());
    }
    return p;
}

QPointF operatorToMachineCorrected(const MachineSetup& setup, const QPointF& operatorUp)
{
    const QPointF m = operatorToMachine(setup, operatorUp);
    return QPointF(m.x() * setup.correctionX, m.y() * setup.correctionY);
}

QPointF machineToOperatorCorrected(const MachineSetup& setup, const QPointF& machine)
{
    return machineToOperator(setup, QPointF(machine.x() / setup.correctionX, machine.y() / setup.correctionY));
}

bool operatorHorizontalIsMachineX(const MachineSetup& setup)
{
    const int r = ((setup.rotationDeg % 360) + 360) % 360;
    return !(r == 90 || r == 270);
}

MachineSetup withoutCorrection(MachineSetup setup)
{
    setup.correctionX = 1.0;
    setup.correctionY = 1.0;
    return setup;
}

QSizeF workAreaInOperatorView(const MachineSetup& setup)
{
    const int r = ((setup.rotationDeg % 360) + 360) % 360;
    return (r == 90 || r == 270) ? QSizeF(setup.workAreaY, setup.workAreaX)
                                 : QSizeF(setup.workAreaX, setup.workAreaY);
}

bool fitsWorkArea(const QSizeF& boundsMm, const MachineSetup& setup)
{
    constexpr double tolerance = 1e-6;
    const QSizeF area = workAreaInOperatorView(setup);
    // Mit Korrekturfaktoren fährt die Maschine entsprechend weiter (waagerecht/senkrecht nach der Aufstellung).
    const bool horizontalIsX = operatorHorizontalIsMachineX(setup);
    const double fw = horizontalIsX ? setup.correctionX : setup.correctionY;
    const double fh = horizontalIsX ? setup.correctionY : setup.correctionX;
    return boundsMm.width() * fw <= area.width() + tolerance && boundsMm.height() * fh <= area.height() + tolerance;
}

double overscanRecommendationMm(const MachineSetup& setup, double angleDeg, double speedMmPerMin)
{
    if (setup.accelerationX <= 0.0 || setup.accelerationY <= 0.0 || speedMmPerMin <= 0.0) {
        return 0.0;
    }
    // Laufrichtung der Zeilen aus Sicht des Bedieners (Y nach oben) in Maschinenachsen umrechnen.
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const QPointF m = operatorToMachine(setup, QPointF(std::cos(rad), std::sin(rad)));
    const double ax = std::abs(m.x()) > 1e-9 ? setup.accelerationX / std::abs(m.x()) : 1e300;
    const double ay = std::abs(m.y()) > 1e-9 ? setup.accelerationY / std::abs(m.y()) : 1e300;
    const double a = std::min(ax, ay);
    const double v = speedMmPerMin / 60.0;
    return v * v / (2.0 * a);
}

double defaultPixelSizeMm(const MachineSetup& setup)
{
    return setup.laserDpi > 0.0 ? 25.4 / setup.laserDpi : 0.0635;
}

// ---------------------------------------------------------------------------------- JSON
QJsonObject machineSetupToJson(const MachineSetup& s)
{
    QJsonObject o;
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = s.name;
    o[QStringLiteral("workAreaX")] = s.workAreaX;
    o[QStringLiteral("workAreaY")] = s.workAreaY;
    o[QStringLiteral("rotationDeg")] = s.rotationDeg;
    o[QStringLiteral("mirrorX")] = s.mirrorX;
    o[QStringLiteral("laserCommand")] = s.laserCommand == LaserCommand::M3 ? QStringLiteral("M3") : QStringLiteral("M4");
    o[QStringLiteral("maxPower")] = s.maxPower;
    o[QStringLiteral("rapidMmPerMin")] = s.rapidMmPerMin;
    o[QStringLiteral("maxSpeedMmPerMin")] = s.maxSpeedMmPerMin;
    o[QStringLiteral("correctionX")] = s.correctionX;
    o[QStringLiteral("correctionY")] = s.correctionY;
    o[QStringLiteral("accelerationX")] = s.accelerationX;
    o[QStringLiteral("accelerationY")] = s.accelerationY;
    o[QStringLiteral("startCode")] = s.startCode;
    o[QStringLiteral("endCode")] = s.endCode;
    o[QStringLiteral("moveToZeroBeforeStart")] = s.moveToZeroBeforeStart;
    o[QStringLiteral("returnToZeroAfterJob")] = s.returnToZeroAfterJob;
    o[QStringLiteral("jogWithJogCommand")] = s.jogWithJogCommand;
    o[QStringLiteral("baudRate")] = s.baudRate;
    o[QStringLiteral("laserDpi")] = s.laserDpi;
    o[QStringLiteral("hasZAxis")] = s.hasZAxis;
    o[QStringLiteral("hasEndstops")] = s.hasEndstops;
    return o;
}

std::optional<MachineSetup> machineSetupFromJson(const QJsonObject& o)
{
    const QString name = o.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || o.value(QStringLiteral("version")).toInt(1) > 1) {
        return std::nullopt;
    }
    const MachineSetup d;   // Vorgaben
    MachineSetup s;
    s.name = name;
    s.workAreaX = std::max(1.0, o.value(QStringLiteral("workAreaX")).toDouble(d.workAreaX));
    s.workAreaY = std::max(1.0, o.value(QStringLiteral("workAreaY")).toDouble(d.workAreaY));
    const int rotation = o.value(QStringLiteral("rotationDeg")).toInt(d.rotationDeg);
    s.rotationDeg = (rotation == 90 || rotation == 180 || rotation == 270) ? rotation : 0;
    s.mirrorX = o.value(QStringLiteral("mirrorX")).toBool(d.mirrorX);
    s.laserCommand = o.value(QStringLiteral("laserCommand")).toString() == QStringLiteral("M3")
                         ? LaserCommand::M3 : LaserCommand::M4;
    s.maxPower = std::max(1, o.value(QStringLiteral("maxPower")).toInt(d.maxPower));
    s.rapidMmPerMin = std::max(1.0, o.value(QStringLiteral("rapidMmPerMin")).toDouble(d.rapidMmPerMin));
    s.correctionX = std::clamp(o.value(QStringLiteral("correctionX")).toDouble(d.correctionX), 0.5, 2.0);
    s.correctionY = std::clamp(o.value(QStringLiteral("correctionY")).toDouble(d.correctionY), 0.5, 2.0);
    s.maxSpeedMmPerMin = std::max(100.0, o.value(QStringLiteral("maxSpeedMmPerMin")).toDouble(d.maxSpeedMmPerMin));
    s.accelerationX = std::max(0.0, o.value(QStringLiteral("accelerationX")).toDouble(d.accelerationX));
    s.accelerationY = std::max(0.0, o.value(QStringLiteral("accelerationY")).toDouble(d.accelerationY));
    s.startCode = o.value(QStringLiteral("startCode")).toString(d.startCode);
    s.endCode = o.value(QStringLiteral("endCode")).toString(d.endCode);
    s.moveToZeroBeforeStart = o.value(QStringLiteral("moveToZeroBeforeStart")).toBool(d.moveToZeroBeforeStart);
    if (o.contains(QStringLiteral("returnToZeroAfterJob"))) {
        s.returnToZeroAfterJob = o.value(QStringLiteral("returnToZeroAfterJob")).toBool(d.returnToZeroAfterJob);
    } else if (o.contains(QStringLiteral("endCode"))) {
        // Altes Profil: Die Rückfahrt stand als "G0 X0 Y0" im Ende-Code. Sie wird jetzt vom Haken gesteuert.
        QStringList kept;
        bool had = false;
        for (const QString& line : s.endCode.split(QLatin1Char('\n'))) {
            if (line.simplified().compare(QStringLiteral("G0 X0 Y0"), Qt::CaseInsensitive) == 0) {
                had = true;
            } else {
                kept << line;
            }
        }
        s.returnToZeroAfterJob = had;
        s.endCode = kept.join(QLatin1Char('\n'));
    }
    s.jogWithJogCommand = o.value(QStringLiteral("jogWithJogCommand")).toBool(d.jogWithJogCommand);
    s.baudRate = std::clamp(o.value(QStringLiteral("baudRate")).toInt(d.baudRate), 1200, 4000000);
    s.laserDpi = std::clamp(o.value(QStringLiteral("laserDpi")).toDouble(d.laserDpi), 10.0, 10000.0);
    s.hasZAxis = o.value(QStringLiteral("hasZAxis")).toBool(d.hasZAxis);
    s.hasEndstops = o.value(QStringLiteral("hasEndstops")).toBool(d.hasEndstops);
    return s;
}

}  // namespace ld::core
