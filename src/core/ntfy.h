// Benachrichtigung über ntfy (https://ntfy.sh): Nachrichten zusammenstellen, ohne Netzwerk und ohne Fenster.
//
// ntfy ist ein einfacher Dienst: Das Programm schickt eine HTTP-Nachricht an ein "Thema" (Topic), die ntfy-App auf dem
// Handy zeigt sie an. Der Themenname ist so gut wie ein Passwort (wer ihn kennt, liest mit) und wird deshalb lang und
// zufällig erzeugt. In den Nachrichten steht nichts Persönliches: keine Dateinamen, Pfade oder Profilnamen.
// Das Senden selbst (QtNetwork) macht app/notificationcontroller.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

#include "core/protocol.h"

namespace ld::core {

struct Notification {
    QString title;
    QString text;
    int priority = 3;      // ntfy: 1 min, 2 niedrig, 3 normal, 4 hoch, 5 dringend
    QStringList tags;      // ntfy-Kürzel für Symbole, zum Beispiel "white_check_mark", "warning"
};

// Standardserver von ntfy.
QString defaultNtfyServer();
// Themenname: "laser-dilettant-" und eine zufällige sechsstellige Zahl (Zufallsgenerator des Systems), zum Beispiel
// "laser-dilettant-048213". Das sind nur etwa 20 Bit Zufall: Wer das Muster kennt, kann ein Thema durch Probieren finden.
QString generateNtfyTopic();
// Der Server ohne Leerzeichen und ohne abschließenden Schrägstrich; ohne "http://" oder "https://" wird "https://" ergänzt.
QString normalizeNtfyServer(const QString& server);
// Gültiger Themenname: 1 bis 64 Zeichen aus Buchstaben, Ziffern, "-" und "_" (so verlangt es ntfy).
bool isValidNtfyTopic(const QString& topic);
// Der JSON-Text für "POST <Server>" (so sind Umlaute in Titel und Text sicher).
QByteArray ntfyJson(const QString& topic, const Notification& notification);

// Laufzeit für den Text, zum Beispiel "1 h 05 min" oder "7 min 30 s".
QString formatDuration(int seconds);

// Nachricht zum Ende eines Auftrags oder keine (std::nullopt):
//   Fertig            -> "Auftrag fertig" (nur wenn notifyFinished und der Auftrag kein Rahmen ist),
//   Alarm, Fehler, Verbindung verloren, GRBL-Reset, anderer Abbruch -> Fehlermeldung (immer),
//   vom Bediener abgebrochen, NOT-AUS (vom Bediener ausgelöst), unterbrochen -> keine Nachricht (der Bediener ist da).
std::optional<Notification> notificationForJobEnd(JobResult result, bool isFrame, int runSeconds, const QString& note, bool notifyFinished);
// Nachricht für ein Problem außerhalb eines Auftrags oder davor (Alarm, Verbindung verloren, GRBL antwortet nicht).
Notification notificationForProblem(const QString& title, const QString& text);
// Probe-Nachricht für den Knopf "Testnachricht senden".
Notification testNotification();

}  // namespace ld::core
