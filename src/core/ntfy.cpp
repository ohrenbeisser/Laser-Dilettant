#include "ntfy.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace ld::core {

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}
}  // namespace

QString defaultNtfyServer()
{
    return QStringLiteral("https://ntfy.sh");
}

QString generateNtfyTopic()
{
    // "laser-dilettant-" und eine sechsstellige Zahl (mit führenden Nullen), zum Beispiel "laser-dilettant-048213".
    const quint32 number = QRandomGenerator::system()->bounded(1000000u);
    return QStringLiteral("laser-dilettant-%1").arg(number, 6, 10, QLatin1Char('0'));
}

QString normalizeNtfyServer(const QString& server)
{
    QString s = server.trimmed();
    if (s.isEmpty()) return defaultNtfyServer();
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    if (!s.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) && !s.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        s = QStringLiteral("https://") + s;
    return s;
}

bool isValidNtfyTopic(const QString& topic)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return valid.match(topic).hasMatch();
}

QByteArray ntfyJson(const QString& topic, const Notification& n)
{
    QJsonObject object;
    object[QStringLiteral("topic")] = topic;
    object[QStringLiteral("title")] = n.title;
    object[QStringLiteral("message")] = n.text;
    object[QStringLiteral("priority")] = n.priority;
    QJsonArray tags;
    for (const QString& tag : n.tags) tags.append(tag);
    object[QStringLiteral("tags")] = tags;
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString formatDuration(int seconds)
{
    seconds = qMax(0, seconds);
    const int h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0) return tr("%1 h %2 min").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    if (m > 0) return tr("%1 min %2 s").arg(m).arg(s);
    return tr("%1 s").arg(s);
}

std::optional<Notification> notificationForJobEnd(JobResult result, bool isFrame, int runSeconds, const QString& note, bool notifyFinished)
{
    Notification n;
    switch (result) {
    case JobResult::Finished:
        if (!notifyFinished || isFrame) return std::nullopt;
        n.title = tr("Laser-Dilettant: Auftrag fertig");
        n.text = tr("Der Auftrag ist fertig. Laufzeit: %1.").arg(formatDuration(runSeconds));
        n.priority = 3;
        n.tags = {QStringLiteral("white_check_mark")};
        return n;
    case JobResult::Alarm:
        n.title = tr("Laser-Dilettant: ALARM");
        n.text = tr("GRBL hat einen Alarm gemeldet, der Auftrag wurde abgebrochen.");
        break;
    case JobResult::Error:
        n.title = tr("Laser-Dilettant: Fehler im Auftrag");
        n.text = tr("Der Auftrag wurde wegen eines Fehlers beendet.");
        break;
    case JobResult::ConnectionLost:
        n.title = tr("Laser-Dilettant: Verbindung verloren");
        n.text = tr("Die Verbindung zum Laser ist abgebrochen. Der Laser kann noch einige Zeilen zu Ende fahren.");
        break;
    case JobResult::GrblReset:
        n.title = tr("Laser-Dilettant: Laser wurde zurückgesetzt");
        n.text = tr("GRBL wurde zurückgesetzt (zum Beispiel Stromausfall des Lasers). Der Auftrag ist beendet.");
        break;
    case JobResult::Aborted:
        n.title = tr("Laser-Dilettant: Auftrag abgebrochen");
        n.text = tr("Der Auftrag wurde abgebrochen.");
        break;
    case JobResult::Running:
    case JobResult::CancelledByUser:
    case JobResult::EmergencyStop:
    case JobResult::Interrupted:
        return std::nullopt;
    }
    Q_UNUSED(note);   // der Hinweis kann Dateinamen enthalten; er bleibt im Protokoll und geht nicht hinaus
    n.priority = 5;
    n.tags = {QStringLiteral("rotating_light")};
    return n;
}

Notification notificationForProblem(const QString& title, const QString& text)
{
    Notification n;
    n.title = tr("Laser-Dilettant: %1").arg(title);
    n.text = text;
    n.priority = 5;
    n.tags = {QStringLiteral("rotating_light")};
    return n;
}

Notification testNotification()
{
    Notification n;
    n.title = tr("Laser-Dilettant: Test");
    n.text = tr("Die Benachrichtigung funktioniert.");
    n.priority = 3;
    n.tags = {QStringLiteral("bell")};
    return n;
}

}  // namespace ld::core
