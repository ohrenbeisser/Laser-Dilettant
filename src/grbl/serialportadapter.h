// Echter serieller Anschluss (QSerialPort) hinter der Schnittstelle IPort.
#pragma once

#include <QList>
#include <QSerialPort>
#include <QString>

#include "grbl/port.h"

namespace ld::grbl {

struct PortInfo {
    QString name;          // zum Beispiel "ttyUSB0" oder "COM3"
    QString description;   // zum Beispiel "USB Serial (CH340)"
};

// Alle gefundenen seriellen Anschlüsse.
QList<PortInfo> availablePorts();

class SerialPortAdapter : public IPort {
    Q_OBJECT

public:
    SerialPortAdapter(const QString& portName, int baudRate, QObject* parent = nullptr);

    bool open(QString* error) override;
    void close() override;
    bool isOpen() const override { return port_.isOpen(); }
    void write(const QByteArray& data) override;
    void flush(int timeoutMs) override;
    QByteArray readAll() override { return port_.readAll(); }
    QString description() const override { return port_.portName(); }

private:
    QSerialPort port_;
    int baud_;
};

}  // namespace ld::grbl
