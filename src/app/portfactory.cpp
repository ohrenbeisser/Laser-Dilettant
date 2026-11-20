#include "portfactory.h"

#include <QCoreApplication>

#include "grbl/serialportadapter.h"
#include "grbl/simulatedgrbl.h"

namespace ld::app {

QString simulatorKey()
{
    return QStringLiteral("simulator");
}

QList<PortChoice> listPortChoices()
{
    QList<PortChoice> list;
    for (const ld::grbl::PortInfo& info : ld::grbl::availablePorts()) {
        list.append({info.name, info.description.isEmpty() ? info.name : QStringLiteral("%1 (%2)").arg(info.name, info.description)});
    }
    list.append({simulatorKey(), QCoreApplication::translate("ld::app", "Simulator (ohne Laser)")});
    return list;
}

ld::grbl::IPort* createPort(const QString& key, int baudRate)
{
    if (key == simulatorKey()) {
        auto* sim = new ld::grbl::SimulatedGrbl;
        sim->setTimeScale(20.0);   // 20-fache Geschwindigkeit: Der Simulator soll nicht ewig dauern
        return sim;
    }
    return new ld::grbl::SerialPortAdapter(key, baudRate);
}

}  // namespace ld::app
