#include "core/DeviceProfile.h"

#include <QtGlobal>

QString DeviceProfile::originToString(Origin o)
{
    switch (o) {
    case Origin::BottomLeft: return QStringLiteral("bottomLeft");
    case Origin::TopLeft: return QStringLiteral("topLeft");
    case Origin::TopRight: return QStringLiteral("topRight");
    case Origin::BottomRight: return QStringLiteral("bottomRight");
    }
    return QStringLiteral("bottomLeft");
}

Origin DeviceProfile::originFromString(const QString& s)
{
    if (s == QLatin1String("topLeft"))
        return Origin::TopLeft;
    if (s == QLatin1String("topRight"))
        return Origin::TopRight;
    if (s == QLatin1String("bottomRight"))
        return Origin::BottomRight;
    return Origin::BottomLeft;
}

QJsonObject DeviceProfile::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("formatVersion")] = kFormatVersion;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("driver")] = driver;
    o[QStringLiteral("baudRate")] = baudRate;
    o[QStringLiteral("workAreaX")] = workAreaX;
    o[QStringLiteral("workAreaY")] = workAreaY;
    o[QStringLiteral("origin")] = originToString(origin);
    o[QStringLiteral("laserType")] = laserType == LaserType::Co2 ? QStringLiteral("co2") : QStringLiteral("diode");
    o[QStringLiteral("laserMode")] = laserMode;
    o[QStringLiteral("maxS")] = maxS;
    o[QStringLiteral("homing")] = homing;
    o[QStringLiteral("airAssist")] = airAssist;
    o[QStringLiteral("focusPower")] = focusPower;
    o[QStringLiteral("maxSpeed")] = maxSpeed;
    o[QStringLiteral("jogSpeed")] = jogSpeed;
    return o;
}

DeviceProfile DeviceProfile::fromJson(const QJsonObject& o)
{
    DeviceProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString(p.id);
    p.driver = o.value(QStringLiteral("driver")).toString(p.driver);
    p.baudRate = o.value(QStringLiteral("baudRate")).toInt(p.baudRate);
    p.workAreaX = o.value(QStringLiteral("workAreaX")).toDouble(p.workAreaX);
    p.workAreaY = o.value(QStringLiteral("workAreaY")).toDouble(p.workAreaY);
    p.origin = originFromString(o.value(QStringLiteral("origin")).toString());
    p.laserType = o.value(QStringLiteral("laserType")).toString() == QLatin1String("co2") ? LaserType::Co2 : LaserType::Diode;
    p.laserMode = o.value(QStringLiteral("laserMode")).toString(p.laserMode);
    p.maxS = o.value(QStringLiteral("maxS")).toDouble(p.maxS);
    p.homing = o.value(QStringLiteral("homing")).toBool(p.homing);
    p.airAssist = o.value(QStringLiteral("airAssist")).toBool(p.airAssist);
    p.focusPower = o.value(QStringLiteral("focusPower")).toDouble(p.focusPower);
    p.maxSpeed = o.value(QStringLiteral("maxSpeed")).toDouble(p.maxSpeed);
    p.jogSpeed = o.value(QStringLiteral("jogSpeed")).toDouble(p.jogSpeed);
    p.sanitize();
    return p;
}

bool DeviceProfile::sanitize()
{
    const DeviceProfile before = *this;
    workAreaX = qBound(1.0, workAreaX, 5000.0);
    workAreaY = qBound(1.0, workAreaY, 5000.0);
    if (laserMode != QLatin1String("M3") && laserMode != QLatin1String("M4"))
        laserMode = QStringLiteral("M4");
    if (!(maxS > 0))
        maxS = 1000.0;
    focusPower = qBound(0.0, focusPower, 100.0);
    maxSpeed = qBound(1.0, maxSpeed, 2000.0);
    jogSpeed = qBound(0.1, jogSpeed, maxSpeed);
    if (baudRate <= 0)
        baudRate = 115200;
    if (driver.isEmpty())
        driver = QStringLiteral("grbl");
    return before.workAreaX == workAreaX && before.workAreaY == workAreaY && before.laserMode == laserMode
        && before.maxS == maxS && before.focusPower == focusPower && before.maxSpeed == maxSpeed
        && before.jogSpeed == jogSpeed && before.baudRate == baudRate && before.driver == driver;
}

int DeviceProfile::screenSignX() const
{
    return (origin == Origin::TopRight || origin == Origin::BottomRight) ? -1 : 1;
}

int DeviceProfile::screenSignY() const
{
    return (origin == Origin::TopLeft || origin == Origin::TopRight) ? -1 : 1;
}
