#include "machineprofiles.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace ld::app {

using ld::core::MachineSetup;

QString MachineProfiles::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/Laser-Dilettant/machines");
}

QString MachineProfiles::defaultMaterialFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/Laser-Dilettant/materials");
}

MachineProfiles::MachineProfiles(const QString& folder, QObject* parent) : QObject(parent), store_(folder) {}

std::optional<MachineSetup> MachineProfiles::load(const QString& name) const
{
    const auto json = store_.read(name);
    return json ? ld::core::machineSetupFromJson(*json) : std::nullopt;
}

bool MachineProfiles::save(const MachineSetup& setup, QString* error)
{
    const bool ok = store_.write(ld::core::machineSetupToJson(setup), error);
    if (ok) {
        emit listChanged();
    }
    return ok;
}

bool MachineProfiles::remove(const QString& name)
{
    const bool ok = store_.remove(name);
    if (ok) {
        emit listChanged();
    }
    return ok;
}

bool MachineProfiles::rename(const QString& oldName, const QString& newName, QString* error)
{
    const bool ok = store_.rename(oldName, newName, error);
    if (ok) {
        if (QString::compare(activeName(), oldName, Qt::CaseInsensitive) == 0) {
            setActive(newName);
        }
        emit listChanged();
    }
    return ok;
}

QString MachineProfiles::activeName() const
{
    const QString remembered = QSettings().value(QStringLiteral("machine/active")).toString();
    const QStringList all = store_.names();
    for (const QString& name : all) {
        if (QString::compare(name, remembered, Qt::CaseInsensitive) == 0) {
            return name;
        }
    }
    return all.isEmpty() ? QString() : all.first();
}

void MachineProfiles::setActive(const QString& name)
{
    QSettings().setValue(QStringLiteral("machine/active"), name);
}

MachineSetup MachineProfiles::active() const
{
    const auto setup = load(activeName());
    return setup ? *setup : MachineSetup{};
}

void MachineProfiles::ensureDefault()
{
    if (!store_.names().isEmpty()) {
        return;
    }
    MachineSetup setup;   // Vorgaben: DIY8000-Desktop1720, X = 500, Y = 400, M4 ...
    // Übernahme der vorläufigen G-Code-Einstellungen (vor M7 in den Programmeinstellungen).
    QSettings legacy;
    if (legacy.contains(QStringLiteral("gcode/maxPower"))) {
        const int rotation = legacy.value(QStringLiteral("gcode/rotation"), 0).toInt();
        setup.rotationDeg = (rotation == 90 || rotation == 180 || rotation == 270) ? rotation : 0;
        setup.mirrorX = legacy.value(QStringLiteral("gcode/mirrorX"), false).toBool();
        setup.laserCommand = legacy.value(QStringLiteral("gcode/laserCommand"), QStringLiteral("M4")).toString() ==
                                     QStringLiteral("M3")
                                 ? ld::core::LaserCommand::M3
                                 : ld::core::LaserCommand::M4;
        setup.maxPower = qMax(1, legacy.value(QStringLiteral("gcode/maxPower"), setup.maxPower).toInt());
        setup.rapidMmPerMin = qMax(1.0, legacy.value(QStringLiteral("gcode/rapid"), setup.rapidMmPerMin).toDouble());
        setup.startCode = legacy.value(QStringLiteral("gcode/startCode"), setup.startCode).toString();
        setup.endCode = legacy.value(QStringLiteral("gcode/endCode"), setup.endCode).toString();
    }
    store_.write(ld::core::machineSetupToJson(setup));
    setActive(setup.name);
}

}  // namespace ld::app
