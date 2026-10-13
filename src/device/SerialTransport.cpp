#include "device/SerialTransport.h"

#include <QCoreApplication>

SerialTransport::SerialTransport(const QString& portName, int baudRate, QObject* parent)
    : ITransport(parent)
    , m_portName(portName)
    , m_baudRate(baudRate)
{
    connect(&m_port, &QSerialPort::readyRead, this, [this] { emit dataReceived(m_port.readAll()); });
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialTransport::onError);
}

SerialTransport::~SerialTransport()
{
    close();
}

QString SerialTransport::hintForError(QSerialPort::SerialPortError error, const QString& portName)
{
    switch (error) {
    case QSerialPort::PermissionError:
#ifdef Q_OS_LINUX
        return QCoreApplication::translate("SerialTransport",
                   "No permission to open %1.\n\n"
                   "Add your user to the group \"dialout\":\n"
                   "    sudo usermod -aG dialout $USER\n"
                   "Then log out and back in.\n\n"
                   "The port may also be in use by another program.")
            .arg(portName);
#else
        return QCoreApplication::translate("SerialTransport",
                   "Access to %1 was denied. The port is probably in use by another program "
                   "(e.g. Engraver Master).")
            .arg(portName);
#endif
    case QSerialPort::DeviceNotFoundError:
#ifdef Q_OS_WIN
        return QCoreApplication::translate("SerialTransport",
                   "Port %1 was not found. Is the laser plugged in and the CH340 driver installed?")
            .arg(portName);
#else
        return QCoreApplication::translate("SerialTransport",
                   "Port %1 was not found. Is the laser plugged in?")
            .arg(portName);
#endif
    case QSerialPort::ResourceError:
        return QCoreApplication::translate("SerialTransport",
                   "The connection to %1 was lost (cable unplugged or device switched off?).")
            .arg(portName);
    default:
        return {};
    }
}

bool SerialTransport::open(QString* errorMessage)
{
    m_port.setPortName(m_portName);
    m_port.setBaudRate(m_baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port.open(QIODevice::ReadWrite)) {
        if (errorMessage) {
            const QString hint = hintForError(m_port.error(), m_portName);
            *errorMessage = hint.isEmpty() ? m_port.errorString() : hint;
        }
        return false;
    }
    // The Arduino resets when DTR goes active; the driver then waits for the
    // GRBL banner.
    m_port.setDataTerminalReady(true);
    m_port.clear();
    return true;
}

void SerialTransport::close()
{
    if (m_port.isOpen())
        m_port.close();
}

bool SerialTransport::isOpen() const
{
    return m_port.isOpen();
}

void SerialTransport::write(const QByteArray& data)
{
    if (m_port.isOpen())
        m_port.write(data);
}

QString SerialTransport::description() const
{
    return QStringLiteral("%1 @ %2").arg(m_portName).arg(m_baudRate);
}

void SerialTransport::onError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError)
        return;
    const bool fatal = error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError;
    QString msg = hintForError(error, m_portName);
    if (msg.isEmpty())
        msg = m_port.errorString();
    if (fatal && m_port.isOpen())
        m_port.close();
    emit errorOccurred(msg, fatal);
}
