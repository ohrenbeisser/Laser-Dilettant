#include "notificationcontroller.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QUrl>

#include "appsettings.h"
#include "connectioncontroller.h"
#include "protocolcontroller.h"

namespace ld::app {

using ld::core::LogLevel;

namespace {
constexpr int kTimeoutMs = 10000;      // so lange wartet das Senden höchstens
constexpr qint64 kDuplicateMs = 5000;  // Fehlernachrichten innerhalb dieser Zeit zählen als eine
}  // namespace

NotificationController::NotificationController(JobController* jobs, ConnectionController* connection, ProtocolController* protocol,
                                               QObject* parent)
    : QObject(parent), protocol_(protocol)
{
    connect(jobs, &JobController::ended, this,
            [this](JobController::Kind kind, ld::core::JobResult result, const QString& note, double runSeconds, double, int) {
                onJobEnded(kind, result, note, runSeconds);
            });
    connect(connection, &ConnectionController::problem, this, &NotificationController::onProblem);
}

void NotificationController::onJobEnded(JobController::Kind kind, ld::core::JobResult result, const QString& note, double runSeconds)
{
    const AppSettings& s = AppSettings::instance();
    const auto notification = ld::core::notificationForJobEnd(result, kind == JobController::Kind::Frame, static_cast<int>(runSeconds), note,
                                                              s.ntfyNotifyFinished());
    if (notification) sendConfigured(*notification, result != ld::core::JobResult::Finished);
}

void NotificationController::onProblem(const QString& title, const QString& text)
{
    sendConfigured(ld::core::notificationForProblem(title, text), true);
}

void NotificationController::sendConfigured(const ld::core::Notification& notification, bool isError)
{
    const AppSettings& s = AppSettings::instance();
    if (!s.ntfyEnabled() || !ld::core::isValidNtfyTopic(s.ntfyTopic())) return;
    if (isError) {
        if (lastError_.isValid() && lastError_.elapsed() < kDuplicateMs) return;   // gleiche Störung, schon gemeldet
        lastError_.start();
    }
    const QString title = notification.title;
    post(s.ntfyServer(), s.ntfyTopic(), notification, [this, title](bool ok, const QString& text) {
        if (ok)
            protocol_->log(LogLevel::Info, tr("Benachrichtigung"), tr("Nachricht gesendet: %1").arg(title));
        else
            protocol_->log(LogLevel::Warning, tr("Benachrichtigung"), tr("Nachricht nicht gesendet: %1").arg(title), text);
    });
}

void NotificationController::sendTest(const QString& server, const QString& topic)
{
    if (!ld::core::isValidNtfyTopic(topic)) {
        emit testFinished(false, tr("Der Themenname ist ungültig (erlaubt: Buchstaben, Ziffern, - und _, höchstens 64 Zeichen)."));
        return;
    }
    post(server, topic, ld::core::testNotification(), [this](bool ok, const QString& text) { emit testFinished(ok, text); });
}

void NotificationController::post(const QString& server, const QString& topic, const ld::core::Notification& notification,
                                  std::function<void(bool, const QString&)> done)
{
    const QString base = ld::core::normalizeNtfyServer(server);
    if (base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) && !QSslSocket::supportsSsl()) {
        done(false, tr("Verschlüsselte Verbindungen (TLS/OpenSSL) sind auf diesem Rechner nicht verfügbar."));
        return;
    }
    QNetworkRequest request{QUrl(base)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = network_.post(request, ld::core::ntfyJson(topic, notification));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            done(true, tr("Gesendet."));
        } else if (status > 0) {
            done(false, tr("Der Server antwortete mit Fehler %1.").arg(status));
        } else {
            done(false, reply->errorString());
        }
        reply->deleteLater();
    });
}

}  // namespace ld::app
