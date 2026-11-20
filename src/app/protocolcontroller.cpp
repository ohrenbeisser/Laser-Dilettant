#include "protocolcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "connectioncontroller.h"
#include "core/imagedocument.h"
#include "grbl/grblconnection.h"
#include "grbl/port.h"

namespace ld::app {

using ld::core::JobResult;
using ld::core::LogLevel;

QString ProtocolController::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Laser-Dilettant/logs");
}

ProtocolController::ProtocolController(const QString& folder, JobController* jobs, ConnectionController* connection,
                                       ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                                       std::function<QString()> materialName, std::function<QString()> ncFileName,
                                       std::function<bool()> ncMode, QObject* parent)
    : QObject(parent),
      folder_(folder),
      jobs_(jobs),
      connection_(connection),
      document_(document),
      machine_(machine),
      materialName_(std::move(materialName)),
      ncFileName_(std::move(ncFileName)),
      ncMode_(std::move(ncMode)),
      jobLog_(folder),
      systemLog_(folder)
{
    connect(jobs_, &JobController::started, this, &ProtocolController::onJobStarted);
    connect(jobs_, &JobController::ended, this, &ProtocolController::onJobEnded);
    connect(jobs_, &JobController::error, this, [this](const QString& text) { log(LogLevel::Error, tr("Auftrag"), text); });

    // Verbindung: Meldungen des ConnectionControllers (Verbinden, Selbsttest, Alarme, Verlust, "antwortet nicht").
    connect(connection_, &ConnectionController::info, this, [this](const QString& text) { log(LogLevel::Info, tr("Verbindung"), text); });
    connect(connection_, &ConnectionController::error, this, [this](const QString& text) {
        const bool alarm = text.startsWith(QStringLiteral("ALARM"));
        // Die Meldung beginnt mit der (übersetzten) Überschrift "Selbsttest": Beides geht durch tr(), damit es in jeder Sprache zusammenpasst.
        const bool selfTest = text.startsWith(tr("Selbsttest"));
        log(selfTest ? LogLevel::Warning : LogLevel::Error, alarm ? tr("Alarm") : selfTest ? tr("Selbsttest") : tr("Verbindung"), text);
    });
    // Firmware-Bericht nach dem Verbinden: $I, $$ und $# (damit steht der Zustand der Firmware zu jedem Auftrag im Protokoll)
    connect(connection_, &ConnectionController::firmwareReport, this,
            [this](const QString& version, const QString& options, const QMap<int, QString>& settings, const QStringList& parameters) {
                log(LogLevel::Info, tr("Firmware"), tr("GRBL-Version ($I): %1").arg(version), tr("Optionen: %1").arg(options));
                QStringList values;
                for (auto it = settings.begin(); it != settings.end(); ++it) values << QStringLiteral("$%1=%2").arg(it.key()).arg(it.value());
                log(LogLevel::Info, tr("Firmware"), tr("GRBL-Einstellungen ($$): %1 Werte").arg(settings.size()), values.join(QLatin1Char(' ')));
                log(LogLevel::Info, tr("Firmware"), tr("GRBL-Parameter ($#): Koordinatensysteme und Versätze"),
                    parameters.join(QStringLiteral(" · ")));
            });
    // Verbunden und getrennt (mit Port); der Verlust der Verbindung kommt als Fehler von oben.
    connect(connection_->grbl(), &ld::grbl::GrblConnection::stateChanged, this, [this](ld::grbl::GrblConnection::State state) {
        using State = ld::grbl::GrblConnection::State;
        const QString port = connection_->grbl()->port() ? connection_->grbl()->port()->description() : QString();
        if (state == State::Connected) log(LogLevel::Info, tr("Verbindung"), tr("Verbunden"), port);
        if (state == State::Disconnected) log(LogLevel::Info, tr("Verbindung"), tr("Getrennt"), port);
    });
}

void ProtocolController::log(LogLevel level, const QString& category, const QString& message, const QString& details)
{
    ld::core::SystemRecord record;
    record.time = QDateTime::currentDateTime();
    record.level = level;
    record.category = category;
    record.message = message;
    record.details = details;
    systemLog_.append(record);
    emit changed();
}

void ProtocolController::recoverAtStart()
{
    const ld::core::JobRecord fixed = jobLog_.recoverInterrupted();
    if (fixed.id.isEmpty()) return;
    log(LogLevel::Warning, tr("Auftrag"), tr("Auftrag \"%1\" war beim letzten Programmende nicht zu Ende und ist als unterbrochen markiert").arg(fixed.name),
        tr("gestartet %1").arg(fixed.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}

QString ProtocolController::kindText(JobController::Kind kind) const
{
    switch (kind) {
    case JobController::Kind::Frame: return tr("Rahmen");
    case JobController::Kind::MaterialTest: return tr("Materialtest");
    case JobController::Kind::Image:
        if (ncMode_ && ncMode_()) return tr("NC-Datei");
        if (document_->calibrationSpec()) return tr("Kalibrierung");
        if (document_->textSpec()) return tr("Text");
        return tr("Bild");
    case JobController::Kind::None: break;
    }
    return QString();
}

QString ProtocolController::jobName(JobController::Kind kind) const
{
    switch (kind) {
    case JobController::Kind::Frame: return tr("Rahmen");
    case JobController::Kind::MaterialTest: return tr("Materialtest");
    case JobController::Kind::Image: {
        if (ncMode_ && ncMode_()) return ncFileName_ ? ncFileName_() : tr("NC-Datei");
        if (document_->calibrationSpec()) return tr("Kalibrierung");
        if (const auto text = document_->textSpec()) {
            QString first = text->text.simplified();
            if (first.size() > 30) first = first.left(30) + QStringLiteral("…");
            return tr("Text: %1").arg(first);
        }
        if (!document_->sourcePath().isEmpty()) return QFileInfo(document_->sourcePath()).fileName();
        return tr("(ohne Namen)");
    }
    case JobController::Kind::None: break;
    }
    return QString();
}

void ProtocolController::onJobStarted(JobController::Kind kind)
{
    ld::core::JobRecord r;
    r.start = QDateTime::currentDateTime();
    r.name = jobName(kind);
    r.kind = kindText(kind);
    r.profile = machine_.name;
    r.material = materialName_ ? materialName_() : QString();
    r.estimateSeconds = static_cast<int>(jobs_->estimateSeconds() + 0.5);
    if (kind == JobController::Kind::Image && !(ncMode_ && ncMode_())) {
        const ld::core::Operation& op = document_->operation();
        r.mode = op.mode == ld::core::EngraveMode::Full ? tr("Full") : op.mode == ld::core::EngraveMode::Cutting ? tr("Cutting") : tr("Linie");
        r.speedMmPerMin = op.speedMmPerMin;
        r.powerPercent = op.powerPercent;
        r.passes = op.passes;
        r.widthMm = document_->transform().widthMm;
        r.heightMm = document_->transform().heightMm;
    }
    jobLog_.begin(r);
    current_ = r;
    emit changed();
}

void ProtocolController::onJobEnded(JobController::Kind kind, JobResult result, const QString& note, double runSeconds,
                                    double activeSeconds, int lines)
{
    Q_UNUSED(kind);
    if (current_.id.isEmpty()) return;   // zu diesem Ende gab es keinen Beginn (sollte nicht vorkommen)
    current_.end = QDateTime::currentDateTime();
    current_.result = result;
    current_.runSeconds = static_cast<int>(runSeconds + 0.5);
    current_.activeSeconds = static_cast<int>(activeSeconds + 0.5);
    current_.lines = lines;
    current_.note = note;
    jobLog_.finish(current_);
    // Alles, was nicht normal endete, steht zusätzlich im Systemprotokoll (dort sucht man bei Problemen zuerst).
    if (result != JobResult::Finished) {
        const bool bad = result != JobResult::CancelledByUser && result != JobResult::EmergencyStop;   // Handlungen des Bedieners sind Warnungen
        log(bad ? LogLevel::Error : LogLevel::Warning, tr("Auftrag"),
            tr("Auftrag \"%1\": %2").arg(current_.name, ld::core::jobResultText(result)), note);
    }
    current_ = ld::core::JobRecord();
    emit changed();
}

}  // namespace ld::app
