#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Byte connection to a device. Implemented by the serial port and by the
// built-in GRBL simulator so the driver does not care which one it talks to.
class ITransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITransport() override = default;

    virtual bool open(QString* errorMessage) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void write(const QByteArray& data) = 0;
    virtual QString description() const = 0;

signals:
    void dataReceived(const QByteArray& data);
    // A fatal error means the connection is gone (cable unplugged etc.).
    void errorOccurred(const QString& message, bool fatal);
};
