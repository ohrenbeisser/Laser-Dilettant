#include "grbl/serialportadapter.h"

#include <QCoreApplication>
#include <QSerialPortInfo>

namespace ld::grbl {

QList<PortInfo> availablePorts()
{
    QList<PortInfo> result;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        result.append({info.portName(), info.description()});
    }
    return result;
}

SerialPortAdapter::SerialPortAdapter(const QString& portName, int baudRate, QObject* parent)
    : IPort(parent), baud_(baudRate)
{
    port_.setPortName(portName);
    connect(&port_, &QSerialPort::readyRead, this, &IPort::readyRead);
    connect(&port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError ||
            error == QSerialPort::PermissionError || error == QSerialPort::UnknownError) {
            if (port_.isOpen()) emit failed(port_.errorString());
        }
    });
}

bool SerialPortAdapter::open(QString* error)
{
    port_.setBaudRate(baud_);
    port_.setDataBits(QSerialPort::Data8);
    port_.setParity(QSerialPort::NoParity);
    port_.setStopBits(QSerialPort::OneStop);
    port_.setFlowControl(QSerialPort::NoFlowControl);
    if (!port_.open(QIODevice::ReadWrite)) {
        if (error) {
            *error = QCoreApplication::translate("ld::grbl", "Der Port %1 konnte nicht geöffnet werden: %2")
                         .arg(port_.portName(), port_.errorString());
            if (port_.error() == QSerialPort::PermissionError) {
                *error += QCoreApplication::translate(
                    "ld::grbl", " (Unter Linux muss der Benutzer in der Gruppe dialout sein.)");
            }
        }
        return false;
    }
    return true;
}

void SerialPortAdapter::close()
{
    if (port_.isOpen()) {
        port_.waitForBytesWritten(300);
        port_.close();
    }
}

void SerialPortAdapter::write(const QByteArray& data)
{
    port_.write(data);
}

void SerialPortAdapter::flush(int timeoutMs)
{
    if (port_.isOpen()) port_.waitForBytesWritten(timeoutMs);
}

}  // namespace ld::grbl
