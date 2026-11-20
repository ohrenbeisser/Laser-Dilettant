// Anmeldung für die Fernansicht (ohne Netzwerk und Fenster): Passwort-Hash, Sitzungen, Bremse bei falschen Passwörtern.
//
// Das Passwort wird nie im Klartext gespeichert, sondern als PBKDF2-HMAC-SHA256 mit zufälligem Salz. Eine Sitzung ist ein
// zufälliges Token (Cookie) mit Ablaufzeit. Die Zeit wird von außen mitgegeben (Millisekunden), damit Tests sie steuern können.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace ld::core {

struct PasswordRecord {
    QByteArray salt;
    QByteArray hash;
    int iterations = 120000;
    bool isSet() const { return !hash.isEmpty() && !salt.isEmpty(); }
    // Speicherform für die Einstellungen: "Wiederholungen$Salz(hex)$Hash(hex)"
    QString toString() const;
    static PasswordRecord fromString(const QString& text);
};

// PBKDF2 mit HMAC-SHA256, length Bytes (Vorgabe 32).
QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int length = 32);
// Neuer Eintrag mit zufälligem Salz (16 Bytes).
PasswordRecord makePasswordRecord(const QString& password, int iterations = 120000);
bool verifyPassword(const QString& password, const PasswordRecord& record);
// Zufälliges Token (32 Bytes aus dem Zufallsgenerator des Systems, hex).
QString randomToken();

class SessionStore {
public:
    explicit SessionStore(qint64 ttlMs = 12LL * 3600 * 1000) : ttlMs_(ttlMs) {}
    QString create(qint64 nowMs);
    // Gültig, wenn bekannt und nicht abgelaufen (abgelaufene werden gelöscht).
    bool valid(const QString& token, qint64 nowMs);
    void remove(const QString& token) { sessions_.remove(token); }
    void clear() { sessions_.clear(); }
    int count() const { return static_cast<int>(sessions_.size()); }

private:
    qint64 ttlMs_;
    QHash<QString, qint64> sessions_;   // Token -> Ablaufzeit
};

// Zu viele falsche Passwörter von derselben Adresse: eine Zeit lang gesperrt.
class LoginLimiter {
public:
    LoginLimiter(int maxFailures = 5, qint64 windowMs = 10LL * 60 * 1000, qint64 blockMs = 5LL * 60 * 1000)
        : maxFailures_(maxFailures), windowMs_(windowMs), blockMs_(blockMs) {}
    bool isBlocked(const QString& key, qint64 nowMs);
    void recordFailure(const QString& key, qint64 nowMs);
    void recordSuccess(const QString& key) { entries_.remove(key); }

private:
    struct Entry {
        int failures = 0;
        qint64 windowStart = 0;
        qint64 blockedUntil = 0;
    };
    int maxFailures_;
    qint64 windowMs_, blockMs_;
    QHash<QString, Entry> entries_;
};

}  // namespace ld::core
