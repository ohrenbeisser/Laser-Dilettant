#pragma once

#include "device/ITransport.h"

#include <QSerialPort>

class SerialTransport : public ITransport
{
    Q_OBJECT
public:
    SerialTransport(const QString& portName, int baudRate, QObject* parent = nullptr);
    ~SerialTransport() override;

    bool open(QString* errorMessage) override;
    void close() override;
    bool isOpen() const override;
    void write(const QByteArray& data) override;
    QString description() const override;

    // Explanation for common problems (permissions, missing driver).
    static QString hintForError(QSerialPort::SerialPortError error, const QString& portName);

private:
    void onError(QSerialPort::SerialPortError error);

    QSerialPort m_port;
    QString m_portName;
    int m_baudRate;
};
