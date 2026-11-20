// Ein kleiner HTTP/1.1-Parser und Antwortbau für die Fernansicht (ohne Netzwerk, damit er sich testen lässt).
//
// Absicht: nur das, was die Fernansicht braucht (GET und POST mit kleinem Inhalt), und dabei streng. Alles, was nicht
// gebraucht wird (Chunked-Übertragung, riesige Köpfe oder Inhalte, Keep-Alive), wird abgelehnt. Der Server (app/webserver)
// schließt nach jeder Antwort die Verbindung.
#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

namespace ld::core {

struct HttpRequest {
    QString method;                       // "GET", "POST" ...
    QString path;                         // ohne Abfrage, Prozentzeichen aufgelöst
    QString query;                        // nach dem "?", unbearbeitet
    QMap<QString, QByteArray> headers;    // Namen klein geschrieben
    QByteArray body;
    QByteArray header(const QString& lowerCaseName) const { return headers.value(lowerCaseName); }
};

class HttpParser {
public:
    enum class State { NeedMore, Complete, Error };
    static constexpr int kMaxHeaderBytes = 16 * 1024;   // Anfragezeile und Köpfe zusammen
    static constexpr int kMaxBodyBytes = 16 * 1024;
    static constexpr int kMaxHeaderCount = 64;

    // Hängt empfangene Bytes an und sagt, ob die Anfrage vollständig ist.
    State feed(const QByteArray& data);
    const HttpRequest& request() const { return request_; }
    // Bei Error: der passende HTTP-Status (400, 413, 431, 501).
    int errorStatus() const { return errorStatus_; }

private:
    State fail(int status);
    State parseHead(int headEnd);

    QByteArray buffer_;
    HttpRequest request_;
    bool headParsed_ = false;
    int headSize_ = 0;
    int contentLength_ = 0;
    int errorStatus_ = 0;
};

using HttpHeaders = QList<QPair<QByteArray, QByteArray>>;

// Eine vollständige Antwort. "Content-Length" und "Connection: close" setzt die Funktion selbst.
QByteArray httpResponse(int status, const QByteArray& contentType, const QByteArray& body, const HttpHeaders& extraHeaders = {});
QByteArray httpReasonPhrase(int status);

// Wert eines Cookies aus dem Kopf "Cookie" (leer, wenn nicht vorhanden).
QString httpCookie(const HttpRequest& request, const QString& name);

}  // namespace ld::core
