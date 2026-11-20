#include "http.h"

#include <QUrl>

namespace ld::core {

HttpParser::State HttpParser::fail(int status)
{
    errorStatus_ = status;
    return State::Error;
}

HttpParser::State HttpParser::feed(const QByteArray& data)
{
    if (errorStatus_ != 0) return State::Error;
    buffer_.append(data);
    if (!headParsed_) {
        const int end = buffer_.indexOf("\r\n\r\n");
        if (end < 0) return buffer_.size() > kMaxHeaderBytes ? fail(431) : State::NeedMore;
        if (end + 4 > kMaxHeaderBytes) return fail(431);
        const State state = parseHead(end);
        if (state == State::Error) return state;
    }
    const int have = static_cast<int>(buffer_.size()) - headSize_;
    if (have < contentLength_) return State::NeedMore;
    request_.body = buffer_.mid(headSize_, contentLength_);
    return State::Complete;
}

HttpParser::State HttpParser::parseHead(int headEnd)
{
    const QList<QByteArray> lines = buffer_.left(headEnd).split('\n');
    // Anfragezeile: METHOD SP Ziel SP HTTP/1.x
    const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
    if (parts.size() != 3) return fail(400);
    const QByteArray& version = parts[2];
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return fail(400);
    for (const char c : parts[0]) {
        if (c < 'A' || c > 'Z') return fail(400);   // Methode: Großbuchstaben
    }
    request_.method = QString::fromLatin1(parts[0]);
    const QByteArray target = parts[1];
    if (target.isEmpty() || target[0] != '/') return fail(400);
    const int q = target.indexOf('?');
    const QByteArray rawPath = q < 0 ? target : target.left(q);
    request_.query = q < 0 ? QString() : QString::fromLatin1(target.mid(q + 1));
    request_.path = QUrl::fromPercentEncoding(rawPath);
    if (request_.path.contains(QChar(0))) return fail(400);

    if (lines.size() - 1 > kMaxHeaderCount) return fail(431);
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) return fail(400);
        request_.headers.insert(QString::fromLatin1(line.left(colon).trimmed().toLower()), line.mid(colon + 1).trimmed());
    }
    if (request_.headers.contains(QStringLiteral("transfer-encoding"))) return fail(501);   // Chunked wird nicht unterstützt
    const QByteArray length = request_.header(QStringLiteral("content-length"));
    if (!length.isEmpty()) {
        bool ok = false;
        const qlonglong n = length.toLongLong(&ok);
        if (!ok || n < 0) return fail(400);
        if (n > kMaxBodyBytes) return fail(413);
        contentLength_ = static_cast<int>(n);
    }
    headSize_ = headEnd + 4;
    headParsed_ = true;
    return State::NeedMore;
}

QByteArray httpReasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "Unknown";
    }
}

QByteArray httpResponse(int status, const QByteArray& contentType, const QByteArray& body, const HttpHeaders& extraHeaders)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + httpReasonPhrase(status) + "\r\n";
    if (!contentType.isEmpty()) out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n";
    for (const auto& header : extraHeaders) out += header.first + ": " + header.second + "\r\n";
    out += "\r\n";
    out += body;
    return out;
}

QString httpCookie(const HttpRequest& request, const QString& name)
{
    const QList<QByteArray> cookies = request.header(QStringLiteral("cookie")).split(';');
    for (const QByteArray& cookie : cookies) {
        const QByteArray item = cookie.trimmed();
        const int eq = item.indexOf('=');
        if (eq > 0 && item.left(eq) == name.toLatin1()) return QString::fromLatin1(item.mid(eq + 1));
    }
    return QString();
}

}  // namespace ld::core
