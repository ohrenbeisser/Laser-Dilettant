#include "webauth.h"

#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace ld::core {

QString PasswordRecord::toString() const
{
    if (!isSet()) return QString();
    return QStringLiteral("%1$%2$%3").arg(iterations).arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(hash.toHex()));
}

PasswordRecord PasswordRecord::fromString(const QString& text)
{
    PasswordRecord record;
    const QStringList parts = text.split(QLatin1Char('$'));
    if (parts.size() != 3) return record;
    bool ok = false;
    const int iterations = parts[0].toInt(&ok);
    if (!ok || iterations < 1000) return record;
    record.iterations = iterations;
    record.salt = QByteArray::fromHex(parts[1].toLatin1());
    record.hash = QByteArray::fromHex(parts[2].toLatin1());
    if (record.salt.isEmpty() || record.hash.isEmpty()) return PasswordRecord();
    return record;
}

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int length)
{
    // RFC 8018: Blöcke zu je 32 Bytes; Block i = U1 xor U2 xor ... xor Un mit U1 = HMAC(P, Salz || i), Uj = HMAC(P, Uj-1).
    constexpr int kBlock = 32;
    QByteArray result;
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha256, password);
    for (quint32 block = 1; result.size() < length; ++block) {
        QByteArray index(4, 0);
        index[0] = static_cast<char>(block >> 24);
        index[1] = static_cast<char>(block >> 16);
        index[2] = static_cast<char>(block >> 8);
        index[3] = static_cast<char>(block);
        hmac.reset();
        hmac.addData(salt);
        hmac.addData(index);
        QByteArray u = hmac.result();
        QByteArray t = u;
        for (int i = 1; i < iterations; ++i) {
            hmac.reset();
            hmac.addData(u);
            u = hmac.result();
            for (int j = 0; j < kBlock; ++j) t[j] = static_cast<char>(t[j] ^ u[j]);
        }
        result.append(t);
    }
    return result.left(length);
}

PasswordRecord makePasswordRecord(const QString& password, int iterations)
{
    PasswordRecord record;
    record.iterations = iterations;
    record.salt.resize(16);
    for (int i = 0; i < 16; ++i) record.salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    record.hash = pbkdf2Sha256(password.toUtf8(), record.salt, iterations);
    return record;
}

bool verifyPassword(const QString& password, const PasswordRecord& record)
{
    if (!record.isSet()) return false;
    const QByteArray hash = pbkdf2Sha256(password.toUtf8(), record.salt, record.iterations, static_cast<int>(record.hash.size()));
    // Vergleich in gleichbleibender Zeit (kein früher Abbruch beim ersten Unterschied)
    unsigned char diff = 0;
    for (int i = 0; i < record.hash.size(); ++i) diff |= static_cast<unsigned char>(hash[i] ^ record.hash[i]);
    return diff == 0 && hash.size() == record.hash.size();
}

QString randomToken()
{
    QByteArray bytes(32, 0);
    for (int i = 0; i < 32; ++i) bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}

QString SessionStore::create(qint64 nowMs)
{
    const QString token = randomToken();
    sessions_.insert(token, nowMs + ttlMs_);
    return token;
}

bool SessionStore::valid(const QString& token, qint64 nowMs)
{
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    if (it.value() <= nowMs) {
        sessions_.erase(it);
        return false;
    }
    return true;
}

bool LoginLimiter::isBlocked(const QString& key, qint64 nowMs)
{
    const auto it = entries_.find(key);
    return it != entries_.end() && it->blockedUntil > nowMs;
}

void LoginLimiter::recordFailure(const QString& key, qint64 nowMs)
{
    Entry& e = entries_[key];
    if (e.failures == 0 || nowMs - e.windowStart > windowMs_) {
        e.failures = 0;
        e.windowStart = nowMs;
    }
    if (++e.failures >= maxFailures_) {
        e.blockedUntil = nowMs + blockMs_;
        e.failures = 0;
    }
}

}  // namespace ld::core
