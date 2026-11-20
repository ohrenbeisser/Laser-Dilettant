// NotificationController: schickt Nachrichten über ntfy aufs Handy (siehe docs/planung.md 3.29 und 3.40).
//
// Er hört auf JobController (Ende eines Auftrags) und ConnectionController (Alarm, Verbindung verloren, GRBL antwortet
// nicht) und sendet je nach Einstellung eine Nachricht. Fehler und Alarme kommen immer (wenn ntfy an ist), "Auftrag fertig"
// nur mit dem Haken in den Einstellungen. Die Nachrichten enthalten keine Dateinamen (core/ntfy). Ob und wie gesendet wurde,
// steht im Systemprotokoll (Kategorie "Benachrichtigung").
#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>

#include "core/ntfy.h"
#include "jobcontroller.h"

namespace ld::app {

class ConnectionController;
class ProtocolController;

class NotificationController : public QObject {
    Q_OBJECT

public:
    NotificationController(JobController* jobs, ConnectionController* connection, ProtocolController* protocol, QObject* parent = nullptr);

    // Testnachricht mit den übergebenen (noch nicht gespeicherten) Werten; das Ergebnis kommt als Signal.
    void sendTest(const QString& server, const QString& topic);

signals:
    void testFinished(bool ok, const QString& text);

private:
    void onJobEnded(JobController::Kind kind, ld::core::JobResult result, const QString& note, double runSeconds);
    void onProblem(const QString& title, const QString& text);
    // Sendet nach den gespeicherten Einstellungen (nichts, wenn ntfy aus ist). isError: für das Unterdrücken von Doppelten.
    void sendConfigured(const ld::core::Notification& notification, bool isError);
    // Sendet an Server und Thema; done(ok, Text) wird nach der Antwort gerufen.
    void post(const QString& server, const QString& topic, const ld::core::Notification& notification,
              std::function<void(bool, const QString&)> done);

    QNetworkAccessManager network_;
    ProtocolController* protocol_;
    QElapsedTimer lastError_;   // Fehlernachrichten kurz hintereinander (Alarm im Auftrag meldet zweimal) nur einmal senden
};

}  // namespace ld::app
