#include "remotecontroller.h"

#include <QBuffer>
#include <QJsonObject>
#include <QtGlobal>

#include "appsettings.h"
#include "connectioncontroller.h"
#include "grbl/grblconnection.h"
#include "protocolcontroller.h"

namespace ld::app {

using ld::core::JobResult;
using ld::core::LogLevel;

RemoteController::RemoteController(JobController* jobs, ConnectionController* connection, ProtocolController* protocol, QObject* parent)
    : QObject(parent), jobs_(jobs), connection_(connection), protocol_(protocol), server_(this, this)
{
    connect(jobs_, &JobController::started, this, [this](JobController::Kind) {
        lastResult_ = JobResult::Running;   // ein neuer Auftrag: das alte Ergebnis gilt nicht mehr
        lastProblem_.clear();
    });
    connect(jobs_, &JobController::ended, this,
            [this](JobController::Kind kind, JobResult result, const QString&, double runSeconds, double, int) {
                lastKind_ = kind;
                lastResult_ = result;
                lastRunSeconds_ = runSeconds;
                lastEstimateSeconds_ = jobs_->estimateSeconds();
            });
    connect(connection_, &ConnectionController::problem, this, [this](const QString& title, const QString& text) {
        lastProblem_ = title + QStringLiteral(": ") + text;
    });
}

void RemoteController::applySettings()
{
    const AppSettings& s = AppSettings::instance();
    lastError_.clear();
    if (!s.webEnabled()) {
        if (server_.isListening()) {
            server_.stop();
            protocol_->log(LogLevel::Info, tr("Zugriff"), tr("Fernansicht ausgeschaltet"));
        }
        return;
    }
    if (server_.isListening() && server_.port() == s.webPort()) return;   // läuft schon richtig
    if (server_.start(static_cast<quint16>(s.webPort()), &lastError_)) {
        protocol_->log(LogLevel::Info, tr("Zugriff"), tr("Fernansicht eingeschaltet"), tr("Port %1").arg(server_.port()));
    } else {
        protocol_->log(LogLevel::Warning, tr("Zugriff"), tr("Fernansicht lässt sich nicht starten"), lastError_);
        emit activity(tr("Fernansicht: Port %1 lässt sich nicht öffnen (%2).").arg(s.webPort()).arg(lastError_));
    }
}

bool RemoteController::hasPassword() const
{
    return AppSettings::instance().webPasswordRecord().isSet();
}

bool RemoteController::checkPassword(const QString& password)
{
    return ld::core::verifyPassword(password, AppSettings::instance().webPasswordRecord());
}

bool RemoteController::controlAllowed() const
{
    return AppSettings::instance().webControlAllowed();
}

// Zustand für die Seite: "lauf", "pause", "alarm", "fertig", "aus" (nicht verbunden) oder "leer" (kein Auftrag).
QString RemoteController::stateName(QString* message) const
{
    using ld::grbl::MachineState;
    ld::grbl::GrblConnection* grbl = connection_->grbl();
    if (!grbl->isConnected()) return QStringLiteral("aus");
    if (grbl->isJobActive()) return grbl->status().state == MachineState::Hold ? QStringLiteral("pause") : QStringLiteral("lauf");
    const bool alarmNow = grbl->status().state == MachineState::Alarm;
    const bool failed = lastResult_ == JobResult::Alarm || lastResult_ == JobResult::Error || lastResult_ == JobResult::ConnectionLost ||
                        lastResult_ == JobResult::GrblReset || lastResult_ == JobResult::Aborted;
    if (alarmNow || failed) {
        *message = lastProblem_.isEmpty() ? (alarmNow ? tr("GRBL meldet einen Alarm.") : tr("Der letzte Auftrag wurde nicht zu Ende gebracht."))
                                          : lastProblem_;
        return QStringLiteral("alarm");
    }
    if (lastResult_ == JobResult::Finished && lastKind_ != JobController::Kind::Frame) return QStringLiteral("fertig");
    return QStringLiteral("leer");
}

QJsonObject RemoteController::statusJson()
{
    ld::grbl::GrblConnection* grbl = connection_->grbl();
    QString message;
    const QString state = stateName(&message);
    const bool active = state == QLatin1String("lauf") || state == QLatin1String("pause");
    QJsonObject o;
    o.insert(QStringLiteral("state"), state);
    o.insert(QStringLiteral("message"), message);
    double percent = 0.0, run = 0.0, estimate = 0.0, remaining = 0.0;
    if (active) {
        percent = qBound(0.0, grbl->jobFraction() * 100.0, 100.0);
        run = jobs_->elapsedSeconds();
        estimate = jobs_->estimateSeconds();
        remaining = qMax(0.0, estimate - jobs_->activeSeconds());   // ohne Pausen
    } else if (state == QLatin1String("fertig")) {
        percent = 100.0;
        run = lastRunSeconds_;
        estimate = lastEstimateSeconds_;
    }
    // Bild des Gebrannten: gibt es eins, welche Version, und wie oft die Seite es abholen soll
    const bool preview = previewSource_.available && previewSource_.available();
    o.insert(QStringLiteral("preview"), preview);
    if (preview) o.insert(QStringLiteral("previewRevision"), static_cast<double>(previewSource_.revision ? previewSource_.revision() : 0));
    o.insert(QStringLiteral("previewSeconds"), AppSettings::instance().webPreviewSeconds());
    o.insert(QStringLiteral("percent"), percent);
    o.insert(QStringLiteral("runSeconds"), static_cast<int>(run));
    o.insert(QStringLiteral("estimateSeconds"), static_cast<int>(estimate));
    o.insert(QStringLiteral("remainingSeconds"), static_cast<int>(remaining));
    // Die Werte der Regler im Programm: Startwert (= 100 %) und Einheit, damit die Seite dasselbe anzeigt wie das Programm
    // (Speed in der gewählten Einheit, Leistung in Prozent; bei NC-Aufträgen nur Prozent).
    const LiveBase base = liveBaseSource_ ? liveBaseSource_() : LiveBase();
    if (base.valid && active) {
        const bool perSec = AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec;
        o.insert(QStringLiteral("speedBase"), base.percentOnly ? 100.0 : base.speedMmPerMin);
        o.insert(QStringLiteral("powerBase"), base.percentOnly ? 100.0 : base.powerPercent);
        o.insert(QStringLiteral("speedUnit"), base.percentOnly ? QStringLiteral("%") : perSec ? QStringLiteral("mm/s") : QStringLiteral("mm/min"));
        o.insert(QStringLiteral("speedFactor"), base.percentOnly || !perSec ? 1.0 : 1.0 / 60.0);
        o.insert(QStringLiteral("percentOnly"), base.percentOnly);
        // Die Regler im Programm, genau so (Wert und Bereich); die Seite zeigt dasselbe an und stellt ihre Schieber danach
        o.insert(QStringLiteral("speedValue"), base.speedValue);
        o.insert(QStringLiteral("speedMin"), base.speedMin);
        o.insert(QStringLiteral("speedMax"), base.speedMax);
        o.insert(QStringLiteral("powerValue"), base.powerValue);
        o.insert(QStringLiteral("powerMin"), base.powerMin);
        o.insert(QStringLiteral("powerMax"), base.powerMax);
    }
    o.insert(QStringLiteral("feedPercent"), grbl->feedOverride());
    o.insert(QStringLiteral("powerPercent"), grbl->spindleOverride());
    // aktuelle Werte, wenn GRBL sie meldet (Vorschub in mm/min, Leistung in % von $30)
    if (active) {
        o.insert(QStringLiteral("currentFeed"), grbl->status().feed);
        if (grbl->maxSpindle() > 0) o.insert(QStringLiteral("currentPower"), qRound(100.0 * grbl->status().spindle / grbl->maxSpindle()));
    }
    return o;
}

QString RemoteController::execute(const QString& command, int value)
{
    using ld::grbl::MachineState;
    ld::grbl::GrblConnection* grbl = connection_->grbl();
    if (!grbl->isConnected()) return tr("Keine Verbindung zum Laser.");
    if (!grbl->isJobActive()) return tr("Es läuft kein Auftrag.");
    const bool held = grbl->status().state == MachineState::Hold;
    if (command == QLatin1String("pause")) {
        if (held) return tr("Der Auftrag ist schon angehalten.");
        grbl->feedHold();   // Feed Hold (!); der Laser geht dabei aus
        emit activity(tr("Fernansicht: Auftrag angehalten."));
    } else if (command == QLatin1String("resume")) {
        if (!held) return tr("Der Auftrag ist nicht angehalten.");
        grbl->resume();
        emit activity(tr("Fernansicht: Auftrag fortgesetzt."));
    } else if (command == QLatin1String("cancel")) {
        jobs_->cancelFromRemote();   // Reset und M5, wie der Abbrechen-Knopf nach der Rückfrage
        emit activity(tr("Fernansicht: Auftrag abgebrochen."));
    } else if (command == QLatin1String("feed")) {
        grbl->setFeedOverride(qBound(10, value, 200));
        emit activity(tr("Fernansicht: Speed auf %1 % gestellt.").arg(grbl->feedOverride()));
    } else if (command == QLatin1String("power")) {
        // Wie der Regler im Programm: bis 200 % des Startwerts, aber nie über 100 % der Maschine (GRBL kappt außerdem bei $30).
        const LiveBase base = liveBaseSource_ ? liveBaseSource_() : LiveBase();
        int maxPercent = 200;
        if (base.valid && !base.percentOnly && base.powerPercent > 0.0) maxPercent = qMin(200, qRound(10000.0 / base.powerPercent));
        grbl->setSpindleOverride(qBound(10, value, qMax(10, maxPercent)));
        emit activity(tr("Fernansicht: Leistung auf %1 % gestellt.").arg(grbl->spindleOverride()));
    } else {
        return tr("Unbekannter Befehl.");
    }
    return QString();
}

QByteArray RemoteController::previewPng()
{
    if (!previewSource_.available || !previewSource_.render || !previewSource_.available()) return QByteArray();
    const quint64 revision = previewSource_.revision ? previewSource_.revision() : 0;
    if (cacheValid_ && revision == cachedRevision_) return cachedPng_;   // seit dem letzten Mal nichts Neues gebrannt
    const QImage image = previewSource_.render(800);
    if (image.isNull()) return QByteArray();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    cachedPng_ = png;
    cachedRevision_ = revision;
    cacheValid_ = true;
    return png;
}

void RemoteController::audit(const QString& event, const QString& peer, const QString& details)
{
    const bool bad = event == QLatin1String("Anmeldung fehlgeschlagen") || event == QLatin1String("Gesperrt");
    protocol_->log(bad ? LogLevel::Warning : LogLevel::Info, tr("Zugriff"), tr("%1 von %2").arg(event, peer), details);
    if (event == QLatin1String("Anmeldung")) emit activity(tr("Fernansicht: Anmeldung von %1.").arg(peer));
}

}  // namespace ld::app
