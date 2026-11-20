// Auswahl und Erzeugung der Anschlüsse: echte serielle Ports und der "Simulator (ohne Laser)".
#pragma once

#include <QList>
#include <QString>

namespace ld::grbl {
class IPort;
}

namespace ld::app {

struct PortChoice {
    QString key;    // "simulator" oder der Portname ("ttyUSB0")
    QString text;   // Anzeige in der Auswahl
};

// Gefundene serielle Ports, danach der Simulator.
QList<PortChoice> listPortChoices();
// Schlüssel des Simulators.
QString simulatorKey();
// Erzeugt den Anschluss zum Schlüssel (der Aufrufer gibt ihn an GrblConnection::setPort).
ld::grbl::IPort* createPort(const QString& key, int baudRate);

}  // namespace ld::app
