#include "webserver.h"

#include <QFile>
#include <QSharedPointer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace ld::app {

using ld::core::HttpParser;
using ld::core::HttpRequest;

bool isPrivateAddress(const QHostAddress& raw)
{
    QHostAddress address = raw;
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok = false;
        const quint32 v4 = address.toIPv4Address(&ok);   // "::ffff:192.168.1.5" (IPv4 über einen IPv6-Socket)
        if (ok) address = QHostAddress(v4);
    }
    if (address.isLoopback()) return true;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        return address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8) ||
               address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12) ||
               address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16) ||
               address.isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16);
    }
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        return address.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7) ||     // eindeutige lokale Adressen
               address.isInSubnet(QHostAddress(QStringLiteral("fe80::")), 10);      // Link-lokal
    }
    return false;
}

WebServer::WebServer(WebBackend* backend, QObject* parent) : QObject(parent), backend_(backend)
{
    clock_.start();
    connect(&server_, &QTcpServer::newConnection, this, &WebServer::onNewConnection);
}

bool WebServer::start(quint16 port, QString* errorText)
{
    stop();
    if (!server_.listen(QHostAddress::Any, port)) {
        if (errorText) *errorText = server_.errorString();
        return false;
    }
    return true;
}

void WebServer::stop()
{
    server_.close();
    sessions_.clear();   // beim Ausschalten sind alle angemeldet gewesenen Geräte wieder abgemeldet
}

void WebServer::onNewConnection()
{
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
        const QHostAddress peer = socket->peerAddress();
        if (!isPrivateAddress(peer) || connections_ >= kMaxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++connections_;
        auto parser = QSharedPointer<HttpParser>::create();
        auto* timer = new QTimer(socket);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, socket, [socket] { socket->abort(); });
        timer->start(kConnectionTimeoutMs);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            --connections_;
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, parser, peer] {
            if (socket->property("answered").toBool()) {
                socket->readAll();
                return;
            }
            const HttpParser::State state = parser->feed(socket->readAll());
            if (state == HttpParser::State::NeedMore) return;
            socket->setProperty("answered", true);
            const QByteArray response = state == HttpParser::State::Complete
                ? handle(parser->request(), peer)
                : error(parser->errorStatus(), QStringLiteral("Ungültige Anfrage"));
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
}

ld::core::HttpHeaders WebServer::commonHeaders() const
{
    return {
        {"Cache-Control", "no-store"},
        {"X-Content-Type-Options", "nosniff"},
        {"Referrer-Policy", "no-referrer"},
        {"X-Frame-Options", "DENY"},
        {"Content-Security-Policy",
         "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"},
    };
}

QByteArray WebServer::json(int status, const QJsonObject& object, const ld::core::HttpHeaders& extra) const
{
    ld::core::HttpHeaders headers = commonHeaders();
    headers.append(extra);
    return ld::core::httpResponse(status, "application/json; charset=utf-8", QJsonDocument(object).toJson(QJsonDocument::Compact), headers);
}

// Der Host-Kopf muss eine IP-Adresse oder "localhost" sein. Eine fremde Webseite, deren Name auf diesen Rechner zeigt
// (DNS-Rebinding), schickt ihren eigenen Namen im Host-Kopf und wird so abgewiesen.
static bool hostAllowed(const QByteArray& hostHeader)
{
    QString host = QString::fromLatin1(hostHeader).trimmed();
    if (host.startsWith(QLatin1Char('['))) {
        const int end = host.indexOf(QLatin1Char(']'));
        if (end < 0) return false;
        host = host.mid(1, end - 1);
    } else if (host.count(QLatin1Char(':')) == 1) {
        host = host.left(host.indexOf(QLatin1Char(':')));
    }
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    QHostAddress address;
    return address.setAddress(host);
}

QByteArray WebServer::handle(const HttpRequest& request, const QHostAddress& peer)
{
    if (!hostAllowed(request.header(QStringLiteral("host")))) return error(400, QStringLiteral("Ungültiger Host"));
    if (request.method != QLatin1String("GET") && request.method != QLatin1String("POST")) return error(405, QStringLiteral("Methode nicht erlaubt"));

    if (request.path.startsWith(QLatin1String("/api/"))) {
        QHostAddress shown = peer;
        bool ok = false;
        if (peer.protocol() == QAbstractSocket::IPv6Protocol) {
            const quint32 v4 = peer.toIPv4Address(&ok);
            if (ok) shown = QHostAddress(v4);
        }
        return handleApi(request, shown.toString());
    }
    if (request.method != QLatin1String("GET")) return error(405, QStringLiteral("Methode nicht erlaubt"));
    return staticFile(request.path);
}

QByteArray WebServer::staticFile(const QString& path)
{
    // Nur bekannte Dateien aus den Ressourcen, nie Pfade aus der Anfrage ins Dateisystem.
    static const QRegularExpression fontName(QStringLiteral("^/fonts/[A-Za-z0-9._-]+\\.(woff2|txt)$"));
    QString resource;
    QByteArray type;
    if (path == QLatin1String("/") || path == QLatin1String("/index.html")) {
        resource = QStringLiteral("index.html"); type = "text/html; charset=utf-8";
    } else if (path == QLatin1String("/app.js")) {
        resource = QStringLiteral("app.js"); type = "text/javascript; charset=utf-8";
    } else if (path == QLatin1String("/theme.css") || path == QLatin1String("/basis.css") || path == QLatin1String("/farben.css")) {
        resource = path.mid(1); type = "text/css; charset=utf-8";
    } else if (path == QLatin1String("/logo.svg") || path == QLatin1String("/logo-dunkel.svg")) {
        resource = path.mid(1); type = "image/svg+xml";
    } else if (fontName.match(path).hasMatch()) {
        resource = path.mid(1);
        type = path.endsWith(QLatin1String(".woff2")) ? "font/woff2" : "text/plain; charset=utf-8";
    } else {
        return error(404, QStringLiteral("Nicht gefunden"));
    }
    QFile file(QStringLiteral(":/web/") + resource);
    if (!file.open(QIODevice::ReadOnly)) return error(404, QStringLiteral("Nicht gefunden"));
    ld::core::HttpHeaders headers = commonHeaders();
    // Die Seite selbst darf nicht im Zwischenspeicher bleiben (sie ändert sich mit dem Programm); Schriften schon.
    if (type == "font/woff2") headers[0] = {"Cache-Control", "public, max-age=86400"};
    return ld::core::httpResponse(200, type, file.readAll(), headers);
}

QByteArray WebServer::handleApi(const HttpRequest& request, const QString& peer)
{
    const bool post = request.method == QLatin1String("POST");
    QJsonObject body;
    if (post) {
        // Schutz vor Anfragen fremder Seiten: Ein Browser schickt diesen eigenen Kopf und JSON nur nach einer Vorabfrage (CORS),
        // die dieser Server nicht beantwortet.
        if (request.header(QStringLiteral("x-ld-request")) != "1" ||
            !request.header(QStringLiteral("content-type")).toLower().startsWith("application/json"))
            return error(400, QStringLiteral("Ungültige Anfrage"));
        const QJsonDocument doc = QJsonDocument::fromJson(request.body);
        if (!request.body.isEmpty() && !doc.isObject()) return error(400, QStringLiteral("Ungültiger Inhalt"));
        body = doc.object();
    }

    if (request.path == QLatin1String("/api/login")) {
        if (!post) return error(405, QStringLiteral("Methode nicht erlaubt"));
        if (!backend_->hasPassword()) return error(503, QStringLiteral("In Laser-Dilettant ist noch kein Passwort gesetzt."));
        if (limiter_.isBlocked(peer, nowMs())) {
            backend_->audit(QStringLiteral("Gesperrt"), peer, QStringLiteral("zu viele falsche Passwörter"));
            return error(429, QStringLiteral("Zu viele Versuche. Bitte einige Minuten warten."));
        }
        if (!backend_->checkPassword(body.value(QStringLiteral("password")).toString())) {
            limiter_.recordFailure(peer, nowMs());
            backend_->audit(QStringLiteral("Anmeldung fehlgeschlagen"), peer, QString());
            return error(401, QStringLiteral("Falsches Passwort"));
        }
        limiter_.recordSuccess(peer);
        const QString token = sessions_.create(nowMs());
        backend_->audit(QStringLiteral("Anmeldung"), peer, QString());
        return json(200, QJsonObject{{QStringLiteral("ok"), true}},
                    {{"Set-Cookie", "ld_session=" + token.toLatin1() + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=43200"}});
    }

    // Ab hier nur mit gültiger Sitzung.
    const QString token = ld::core::httpCookie(request, QStringLiteral("ld_session"));
    if (!sessions_.valid(token, nowMs())) return error(401, QStringLiteral("Anmeldung erforderlich"));

    if (request.path == QLatin1String("/api/logout")) {
        if (!post) return error(405, QStringLiteral("Methode nicht erlaubt"));
        sessions_.remove(token);
        backend_->audit(QStringLiteral("Abmeldung"), peer, QString());
        return json(200, QJsonObject{{QStringLiteral("ok"), true}}, {{"Set-Cookie", "ld_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0"}});
    }
    if (request.path == QLatin1String("/api/status")) {
        if (post) return error(405, QStringLiteral("Methode nicht erlaubt"));
        QJsonObject status = backend_->statusJson();
        status.insert(QStringLiteral("control"), backend_->controlAllowed());
        return json(200, status);
    }
    if (request.path == QLatin1String("/api/preview")) {
        if (post) return error(405, QStringLiteral("Methode nicht erlaubt"));
        const QByteArray png = backend_->previewPng();
        if (png.isEmpty()) return error(404, QStringLiteral("Noch kein Bild"));
        return ld::core::httpResponse(200, "image/png", png, commonHeaders());
    }
    if (request.path == QLatin1String("/api/command")) {
        if (!post) return error(405, QStringLiteral("Methode nicht erlaubt"));
        if (!backend_->controlAllowed()) return error(403, QStringLiteral("Die Fernsteuerung ist am PC nicht freigegeben."));
        const QString command = body.value(QStringLiteral("command")).toString();
        const int value = body.value(QStringLiteral("value")).toInt();
        const QString problem = backend_->execute(command, value);
        backend_->audit(QStringLiteral("Befehl"), peer,
                        problem.isEmpty() ? QStringLiteral("%1 %2").arg(command).arg(value)
                                          : QStringLiteral("%1 %2 abgelehnt: %3").arg(command).arg(value).arg(problem));
        if (!problem.isEmpty()) return error(409, problem);
        return json(200, QJsonObject{{QStringLiteral("ok"), true}});
    }
    return error(404, QStringLiteral("Nicht gefunden"));
}

}  // namespace ld::app
