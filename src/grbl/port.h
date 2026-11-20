// Schnittstelle zu einem seriellen Anschluss. Die Verbindung (GrblConnection) kennt nur diese
// Schnittstelle; in der App steckt dahinter ein echter serieller Port, in den Tests und im
// "Simulator (ohne Laser)" ein simulierter GRBL-Controller.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace ld::grbl {

class IPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Öffnet den Anschluss. false mit Fehlertext, wenn es nicht klappt.
    virtual bool open(QString* error) = 0;
    // Schließt den Anschluss; noch nicht gesendete Bytes werden vorher möglichst noch geschrieben.
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void write(const QByteArray& data) = 0;
    // Wartet höchstens timeoutMs, bis das Geschriebene wirklich auf der Leitung ist, OHNE die Ereignisschleife zu
    // starten (beim Trennen darf nichts dazwischenkommen). Der simulierte Anschluss braucht nichts zu tun.
    virtual void flush(int timeoutMs) { Q_UNUSED(timeoutMs); }
    virtual QByteArray readAll() = 0;
    virtual QString description() const = 0;

signals:
    void readyRead();
    // Der Anschluss ist ausgefallen (zum Beispiel USB-Kabel gezogen).
    void failed(const QString& reason);
};

}  // namespace ld::grbl
