#pragma once

#include <QJsonObject>
#include <QString>

// Corner of the work area where the machine origin (0,0) is located.
enum class Origin { BottomLeft, TopLeft, TopRight, BottomRight };

enum class LaserType { Diode, Co2 };

// Settings that describe one laser machine. Stored as JSON, see section 8.1
// of the specification.
struct DeviceProfile
{
    static constexpr int kFormatVersion = 1;

    QString id;              // file name without extension, unique
    QString name;            // display name
    QString driver = QStringLiteral("grbl");
    int baudRate = 115200;
    double workAreaX = 300.0; // mm
    double workAreaY = 300.0; // mm
    Origin origin = Origin::BottomLeft;
    LaserType laserType = LaserType::Diode;
    QString laserMode = QStringLiteral("M4"); // "M3" or "M4"
    double maxS = 1000.0;     // corresponds to $30
    bool homing = false;
    bool airAssist = false;
    double focusPower = 1.0;  // percent
    double maxSpeed = 100.0;  // mm/s, upper limit for input
    double jogSpeed = 50.0;   // mm/s

    bool builtin = false;     // not stored; set by ProfileStore

    QJsonObject toJson() const;
    static DeviceProfile fromJson(const QJsonObject& obj);

    // Clamps values to sane ranges. Returns false if something had to be fixed.
    bool sanitize();

    // +1 if a positive machine coordinate moves right (X) / up (Y) on screen.
    int screenSignX() const;
    int screenSignY() const;

    static QString originToString(Origin o);
    static Origin originFromString(const QString& s);
};
