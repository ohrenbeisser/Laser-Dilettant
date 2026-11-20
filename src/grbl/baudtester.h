// Sucht die Baudrate, bei der GRBL antwortet (Verbindungs-Selbsttest, siehe planung.md 3.15):
// Probiert gängige Raten nacheinander. Je Rate wird der Port geöffnet, einige Sekunden lang "?" gesendet
// (ein Arduino braucht nach dem Öffnen etwa 2 s zum Starten) und auf eine Antwort gewartet.
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTimer>
#include <functional>

#include "grbl/port.h"

namespace ld::grbl {

class BaudTester : public QObject {
    Q_OBJECT

public:
    // factory erzeugt einen frischen (noch geschlossenen) Port für die Baudrate; BaudTester löscht ihn wieder.
    using PortFactory = std::function<IPort*(int baud)>;

    BaudTester(PortFactory factory, QList<int> bauds, QObject* parent = nullptr, int perBaudMs = 3500);
    ~BaudTester() override;

    // Gängige Raten, die wahrscheinlichste zuerst.
    static QList<int> commonBauds();

    void start();
    void cancel();
    bool isRunning() const { return running_; }

signals:
    void testing(int baud);
    void finished(int foundBaud);        // 0: keine Antwort bei keiner Rate
    void failed(const QString& reason);  // Port ließ sich nicht öffnen

private:
    void next();
    void stopCurrent();
    void onReadyRead();

    PortFactory factory_;
    QList<int> bauds_;
    int perBaudMs_;
    int index_ = -1;
    bool running_ = false;
    IPort* port_ = nullptr;
    QByteArray buffer_;
    QTimer pingTimer_;
    QTimer timeout_;
};

}  // namespace ld::grbl
