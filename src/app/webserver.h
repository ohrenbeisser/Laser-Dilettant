// WebServer: ein kleiner HTTP-Server für die Fernansicht (siehe docs/planung.md 3.41).
//
// Er liefert die Seite (aus den eingebetteten Ressourcen ":/web/...") und eine kleine JSON-Schnittstelle:
//   POST /api/login    {"password": "..."}                  -> Sitzung (Cookie)
//   POST /api/logout
//   GET  /api/status                                         -> Zustand, Fortschritt, Zeiten, Regler
//   POST /api/command  {"command": "pause|resume|cancel|feed|power", "value": n}
// Was er mit dem Laser tut, weiß er nicht: Das macht der WebBackend (RemoteController). So lässt er sich ohne Laser testen.
//
// Absicherung (Absicht: nur im Heimnetz, nur mit Passwort):
//   * Verbindungen von Adressen außerhalb privater Netze werden sofort geschlossen.
//   * Der Host-Kopf muss eine IP-Adresse oder "localhost" sein (Schutz vor DNS-Rebinding).
//   * Alle POST-Anfragen brauchen den Kopf "X-LD-Request: 1" und JSON (Schutz vor fremden Seiten).
//   * Sitzung als Cookie (HttpOnly, SameSite=Strict, 12 Stunden), Passwort nur als PBKDF2-Hash, Bremse bei falschen Passwörtern.
//   * Kleine Grenzen für Köpfe und Inhalt, 10 s Zeitgrenze je Verbindung, höchstens 16 gleichzeitig.
// Es ist KEIN HTTPS: Das Passwort geht im Heimnetz unverschlüsselt über die Leitung (siehe Einstellungen).
#pragma once

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

#include "core/http.h"
#include "core/webauth.h"

namespace ld::app {

// Was der Server vom Rest des Programms braucht.
class WebBackend {
public:
    virtual ~WebBackend() = default;
    virtual bool hasPassword() const = 0;
    virtual bool checkPassword(const QString& password) = 0;
    virtual QJsonObject statusJson() = 0;
    virtual bool controlAllowed() const = 0;
    // Führt einen Befehl aus; leerer Text = gelungen, sonst der Grund für die Ablehnung.
    virtual QString execute(const QString& command, int value) = 0;
    // Das Bild des Gebrannten als PNG (leer, wenn es noch keines gibt).
    virtual QByteArray previewPng() = 0;
    // Ereignis fürs Zugriffsprotokoll ("Anmeldung", "Anmeldung fehlgeschlagen", "Gesperrt", "Abmeldung", "Befehl", ...).
    virtual void audit(const QString& event, const QString& peer, const QString& details) = 0;
};

// Private, lokale und Link-lokale Adressen (Heimnetz); alles andere gilt als fremd.
bool isPrivateAddress(const QHostAddress& address);

class WebServer : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxConnections = 16;
    static constexpr int kConnectionTimeoutMs = 10000;

    explicit WebServer(WebBackend* backend, QObject* parent = nullptr);

    // Port 0: das System wählt einen freien Port (für Tests).
    bool start(quint16 port, QString* error = nullptr);
    void stop();
    bool isListening() const { return server_.isListening(); }
    quint16 port() const { return server_.serverPort(); }

private:
    void onNewConnection();
    QByteArray handle(const ld::core::HttpRequest& request, const QHostAddress& peer);
    QByteArray handleApi(const ld::core::HttpRequest& request, const QString& peerText);
    QByteArray staticFile(const QString& path);
    QByteArray json(int status, const QJsonObject& object, const ld::core::HttpHeaders& extra = {}) const;
    QByteArray error(int status, const QString& message) const { return json(status, QJsonObject{{QStringLiteral("error"), message}}); }
    qint64 nowMs() const { return clock_.elapsed(); }
    ld::core::HttpHeaders commonHeaders() const;

    WebBackend* backend_;
    QTcpServer server_;
    QElapsedTimer clock_;
    ld::core::SessionStore sessions_;
    ld::core::LoginLimiter limiter_;
    int connections_ = 0;
};

}  // namespace ld::app
