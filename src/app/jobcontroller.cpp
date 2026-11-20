#include "jobcontroller.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>
#include <cmath>

#include "appsettings.h"
#include "core/imagedocument.h"
#include "core/objecttransform.h"
#include "core/pathdata.h"
#include "core/pathpreview.h"
#include "grbl/clock.h"
#include "grbl/grblconnection.h"
#include "grbl/weaklight.h"

namespace ld::app {

JobController::JobController(ld::grbl::GrblConnection* grbl, ld::grbl::WeakLight* weak, ld::core::PathPreview* preview,
                             ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                             std::function<bool()> ncMode, QWidget* dialogParent, QObject* parent)
    : QObject(parent),
      grbl_(grbl),
      weak_(weak),
      preview_(preview),
      document_(document),
      machine_(machine),
      ncMode_(std::move(ncMode)),
      dialogParent_(dialogParent)
{
    // Diese drei Signale der Verbindung betreffen jeden Lauf; die Oberfläche hört auf unsere Signale.
    connect(grbl_, &ld::grbl::GrblConnection::jobFinished, this, &JobController::onJobFinished);
    connect(grbl_, &ld::grbl::GrblConnection::jobAborted, this, [this](const QString& reason) { onJobAborted(reason); });
    connect(grbl_, &ld::grbl::GrblConnection::jobError, this, &JobController::onJobError);
    connect(grbl_, &ld::grbl::GrblConnection::jobProgress, this, [this](double fraction) {
        if (!isFrameLike() || isMaterialTest()) emit progress(static_cast<int>(std::lround(fraction * 100.0)));
    });
    connect(grbl_, &ld::grbl::GrblConnection::alarm, this, [this](int code, const QString&, bool) { alarmCode_ = code; });
}

double JobController::estimateSeconds() const
{
    if (kind_ == Kind::MaterialTest) return testSeconds_;
    if (kind_ == Kind::Image) return preview_->stats().seconds;
    return 0.0;
}

void JobController::resetProtocolState()
{
    activeMs_ = 0;
    lastStatusMs_ = 0;
    lastWasRunning_ = false;
    cancelRequested_ = false;
    emergency_ = false;
    alarmCode_ = -1;
    errorNote_.clear();
}

// ---------------------------------------------------------------------------------------------
// Fragen vor dem Start (an einer Stelle, damit sie nicht auseinanderlaufen)
// ---------------------------------------------------------------------------------------------
bool JobController::checkIdle(const QString& title)
{
    if (grbl_->status().state == ld::grbl::MachineState::Idle) return true;
    QMessageBox::information(dialogParent_, title,
                             tr("GRBL ist nicht bereit (Zustand: %1). Bei einem Alarm zuerst den Alarm aufheben.")
                                 .arg(ld::grbl::stateName(grbl_->status().state)));
    return false;
}

bool JobController::confirmPosition()
{
    if (!grbl_->positionUncertain()) return true;
    if (QMessageBox::question(dialogParent_, tr("Position prüfen"),
                              tr("Nach einem Not-Aus oder Alarm kann die Position des Lasers ungenau sein.\n\n"
                                 "Ist der Nullpunkt noch richtig? Sonst bitte abbrechen und \"Hier ist Null\" neu setzen."),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return false;
    }
    grbl_->confirmPosition();
    return true;
}

bool JobController::confirmStartWarning()
{
    if (!AppSettings::instance().showStartWarning()) return true;
    QMessageBox box(QMessageBox::Warning, tr("Vor dem Start"),
                    tr("Bitte prüfen:\n\n• Schutzbrille aufgesetzt\n• Material fixiert\n• Nullpunkt gesetzt\n"
                       "• Nichts Brennbares im Arbeitsbereich, Laser beaufsichtigen"),
                    QMessageBox::Ok | QMessageBox::Cancel, dialogParent_);
    auto* never = new QCheckBox(tr("Diesen Hinweis nicht mehr anzeigen"));
    box.setCheckBox(never);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Ok) return false;
    if (never->isChecked()) AppSettings::instance().setShowStartWarning(false);
    return true;
}

bool JobController::launch(const QString& title, const QString& path)
{
    QString error;
    weak_->turnOff(true);   // vor dem Lauf muss das Schwachlicht aus sein (M5 geht dem Auftrag voraus)
    if (!grbl_->startJob(path, &error)) {
        QMessageBox::warning(dialogParent_, title, error);
        return false;
    }
    resetProtocolState();
    timer_.start();   // Laufzeit für alle Läufe (auch der Rahmen)
    return true;
}

// ---------------------------------------------------------------------------------------------
// Bild bzw. NC-Datei
// ---------------------------------------------------------------------------------------------
void JobController::requestStart()
{
    using State = ld::core::PathPreview::State;
    if (!checkIdle(tr("Start")) || !confirmPosition() || !confirmStartWarning()) return;
    switch (preview_->state()) {
    case State::Ready:
        beginImageJob();
        break;
    case State::Stale:
        startPending_ = true;
        preview_->request();
        emit previewRequested();
        break;
    case State::Generating:
    case State::Loading:
        startPending_ = true;
        break;
    case State::Failed:
        QMessageBox::warning(dialogParent_, tr("Start"), preview_->error());
        break;
    case State::Idle:
        emit message(tr("Das Bild wird noch berechnet. Bitte einen Moment warten."), 4000);
        break;
    }
}

bool JobController::onPreviewReady()
{
    if (!startPending_) return false;
    startPending_ = false;
    beginImageJob();
    return true;
}

bool JobController::onPreviewFailed(const QString& error)
{
    if (!startPending_) return false;
    startPending_ = false;
    QMessageBox::warning(dialogParent_, tr("Start"), error);
    return true;
}

void JobController::beginImageJob()
{
    const auto data = preview_->data();
    const QString path = preview_->filePath();
    if (!data || data->segments.empty() || path.isEmpty()) {
        emit message(tr("Es gibt nichts zu gravieren: Der Auftrag enthält keine Gravur."), 5000);
        return;
    }
    if (!launch(tr("Start"), path)) return;
    kind_ = Kind::Image;
    progressIndex_ = 0;
    timer_.start();
    emit started(kind_);
    emit info(tr("Auftrag gestartet (%1).").arg(QFileInfo(path).fileName()));
    emit message(tr("Auftrag läuft."), 4000);
}

void JobController::confirmCancel()
{
    if (!grbl_->isJobActive()) return;
    if (QMessageBox::question(dialogParent_, tr("Auftrag abbrechen"),
                              tr("Den Auftrag abbrechen? Der Laser wird ausgeschaltet (Reset und M5); die Position kann danach ungenau sein."),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
        cancelRequested_ = true;
        grbl_->cancelJob();
    }
}

void JobController::cancelFromRemote()
{
    if (!grbl_->isJobActive()) return;
    cancelRequested_ = true;
    grbl_->cancelJob();
}

// ---------------------------------------------------------------------------------------------
// Rahmen
// ---------------------------------------------------------------------------------------------
void JobController::startFrame(double speedMmPerMin, double powerPercent)
{
    if (!grbl_->isConnected() || grbl_->isJobActive()) return;
    if (ncMode_ && ncMode_()) {
        // NC-Auftrag: Rahmen um das umschließende Rechteck aller Wege (Nullpunkt der Datei als Ursprung).
        // NC-Wege werden unkorrigiert gesendet, also auch der Rahmen um sie.
        const auto data = preview_->data();
        if (!data || data->segments.empty()) return;
        startFrameFromResult(ld::core::generateFrameGcodeForRect(ld::core::withoutCorrection(machine_), data->bounds,
                                                                 speedMmPerMin, powerPercent));
        return;
    }
    if (!document_->hasImage()) return;
    startFrameFromResult(ld::core::generateFrameGcode(document_->makeGcodeJob(machine_), speedMmPerMin, powerPercent));
}

void JobController::startMaterialTestFrame(const ld::core::MaterialTestSpec& spec, double speedMmPerMin, double powerPercent)
{
    if (!grbl_->isConnected() || grbl_->isJobActive()) return;
    startFrameFromResult(
        ld::core::generateFrameGcode(ld::core::materialTestFrameJob(spec, machine_), speedMmPerMin, powerPercent));
}

void JobController::startFrameFromResult(const ld::core::FrameResult& frame)
{
    if (!frame.ok) {
        QMessageBox::warning(dialogParent_, tr("Rahmen"), frame.error);
        return;
    }
    // Keine Warnung vor dem Rahmen (Auftraggeber, 2026-11-20); gefragt wird nur, wenn GRBL nicht bereit ist oder die Position unsicher ist.
    if (!checkIdle(tr("Rahmen")) || !confirmPosition()) return;
    QFile file(dir_.filePath(QStringLiteral("frame.nc")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(frame.gcode) != frame.gcode.size()) {
        QMessageBox::warning(dialogParent_, tr("Rahmen"), tr("Die Rahmen-Datei konnte nicht geschrieben werden."));
        return;
    }
    file.close();
    if (!launch(tr("Rahmen"), file.fileName())) return;
    kind_ = Kind::Frame;
    emit started(kind_);
    emit info(tr("Rahmen wird abgefahren."));
    emit message(tr("Rahmen wird abgefahren."), 4000);
}

// ---------------------------------------------------------------------------------------------
// Materialtest
// ---------------------------------------------------------------------------------------------
void JobController::startMaterialTest(const ld::core::MaterialTestSpec& spec)
{
    if (!grbl_->isConnected() || grbl_->isJobActive()) return;
    if (!checkIdle(tr("Materialtest"))) return;
    double maxPower = 0.0;
    for (const double p : spec.powers) maxPower = std::max(maxPower, p);
    if (QMessageBox::warning(dialogParent_, tr("Materialtest"),
                             tr("Der Materialtest brennt %1 Felder mit einer Power bis %2 %. Schutzbrille aufsetzen, das Material "
                                "fixieren und den Nullpunkt prüfen. Der Test gehört auf Restmaterial.")
                                 .arg(spec.cells * spec.cells)
                                 .arg(maxPower, 0, 'f', 0),
                             QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok) {
        return;
    }
    if (!confirmPosition()) return;
    QFile file(dir_.filePath(QStringLiteral("materialtest.nc")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(dialogParent_, tr("Materialtest"), tr("Die Datei für den Materialtest konnte nicht geschrieben werden."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ld::core::GcodeResult result = ld::core::generateMaterialTestGcode(
        spec, machine_, [&](const QByteArray& chunk) { return file.write(chunk) == chunk.size(); });
    QApplication::restoreOverrideCursor();
    file.close();
    if (!result.ok) {
        QMessageBox::warning(dialogParent_, tr("Materialtest"),
                             result.error.isEmpty() ? tr("Der G-Code konnte nicht erzeugt werden.") : result.error);
        return;
    }
    if (!launch(tr("Materialtest"), file.fileName())) return;
    kind_ = Kind::MaterialTest;
    testSeconds_ = result.stats.seconds;
    timer_.start();
    emit started(kind_);
    emit info(tr("Materialtest gestartet (%1 Felder).").arg(spec.cells * spec.cells));
    emit message(tr("Materialtest läuft."), 4000);
}

// ---------------------------------------------------------------------------------------------
// Laufende Aufträge
// ---------------------------------------------------------------------------------------------
void JobController::trackLaser(const ld::grbl::GrblStatus& status)
{
    // Arbeitszeit: Die Zeit zwischen zwei Statusmeldungen zählt, wenn GRBL zuvor im Zustand "Läuft" war.
    if (kind_ != Kind::None) {
        const qint64 now = ld::grbl::monotonicMs();
        if (lastWasRunning_ && lastStatusMs_ > 0) activeMs_ += now - lastStatusMs_;
        lastStatusMs_ = now;
        lastWasRunning_ = status.state == ld::grbl::MachineState::Run;
    }
    const bool nc = ncMode_ && ncMode_();
    if (kind_ == Kind::MaterialTest && grbl_->isJobActive()) {
        emit durationText(tr("Materialtest %1 von ca. %2")
                              .arg(ld::core::formatDuration(elapsedSeconds()),
                                   ld::core::formatDuration(testSeconds_ * 100.0 / std::max(10, grbl_->feedOverride()))));
    }
    if (!status.hasPosition || (!nc && !document_->hasImage())) return;
    const QSizeF bounds = ld::core::boundingSize(document_->transform());
    // NC-Auftrag: Nullpunkt der Datei = Ursprung der Anzeige (keine Größe, Ursprung oben links)
    const QPointF scene =
        nc ? ld::core::machineToScene(ld::core::withoutCorrection(machine_), ld::core::JobOrigin::TopLeft, QSizeF(), status.wpos)
           : ld::core::machineToScene(document_->calibrationSpec() ? ld::core::withoutCorrection(machine_) : machine_,
                                      document_->jobOrigin(), bounds, status.wpos,
                                      QPointF(document_->transform().offsetXMm, document_->transform().offsetYMm));
    if (grbl_->isJobActive() && !isFrameLike()) {
        // Die gemeldete Position wird dem Weg zugeordnet; bis dahin gilt alles als gelasert.
        const auto data = preview_->data();
        if (data) {
            const size_t index = ld::core::locateOnPath(*data, progressIndex_, scene);
            if (index > progressIndex_) {
                emit pathAdvanced(progressIndex_, index, scene);
                progressIndex_ = index;
            } else {
                emit headMoved(scene);
            }
            emit durationText(tr("Lauf %1 von ca. %2")
                                  .arg(ld::core::formatDuration(elapsedSeconds()),
                                       ld::core::formatDuration(data->totalSeconds * 100.0 / std::max(10, grbl_->feedOverride()))));
        }
    } else {
        emit headIdle(scene);
    }
}

// Meldet das Ende an das Protokoll (einmal je Auftrag); danach ist kein Auftrag mehr aktiv.
void JobController::endJob(ld::core::JobResult result, const QString& note)
{
    if (kind_ == Kind::None) return;
    // Die letzte Statusmeldung zählt bis jetzt, wenn GRBL zuletzt "Läuft" meldete.
    if (lastWasRunning_ && lastStatusMs_ > 0) activeMs_ += ld::grbl::monotonicMs() - lastStatusMs_;
    emit ended(kind_, result, note, elapsedSeconds(), activeSeconds(), static_cast<int>(grbl_->jobLinesAcknowledged()));
}

void JobController::onJobFinished()
{
    const Kind finishedKind = kind_;
    endJob(ld::core::JobResult::Finished, QString());
    kind_ = Kind::None;
    emit finished(finishedKind);
}

void JobController::onJobAborted(const QString& reason)
{
    // Warum ist der Auftrag zu Ende? NOT-AUS, Alarm und die Wahl des Bedieners sind bekannt; sonst sagt es der Grund.
    using ld::core::JobResult;
    JobResult result = JobResult::Aborted;
    QString note = reason;
    if (emergency_) {
        result = JobResult::EmergencyStop;
    } else if (alarmCode_ >= 0) {
        result = JobResult::Alarm;
        note = tr("Alarm %1").arg(alarmCode_);
    } else if (cancelRequested_) {
        result = JobResult::CancelledByUser;
        if (!errorNote_.isEmpty()) {
            result = JobResult::Error;
            note = errorNote_;
        }
    } else if (!errorNote_.isEmpty()) {
        result = JobResult::Error;
        note = errorNote_;
    } else if (reason == QCoreApplication::translate("ld::grbl::GrblConnection", "Die Verbindung wurde getrennt.") ||
               reason == QCoreApplication::translate("ld::grbl::GrblConnection", "Die Verbindung ist abgebrochen.")) {
        // Die Texte kommen aus GrblConnection (dieselbe Übersetzung), damit der Vergleich in jeder Sprache stimmt.
        result = JobResult::ConnectionLost;
    } else if (reason == QCoreApplication::translate("ld::grbl::GrblConnection", "GRBL wurde zurückgesetzt.")) {
        result = JobResult::GrblReset;
    }
    endJob(result, note);
    kind_ = Kind::None;
}

void JobController::onJobError(long long lineNumber, int code, const QString& text, const QString& line)
{
    // Das Senden ist schon gestoppt (Feed Hold). Der Bediener entscheidet.
    const QString message = tr("Zeile %1: Fehler %2: %3\n\n%4").arg(lineNumber).arg(code).arg(text, line);
    errorNote_ = tr("Zeile %1: Fehler %2: %3").arg(lineNumber).arg(code).arg(text);
    emit error(tr("Fehler im Auftrag, ") + message);
    QMessageBox box(QMessageBox::Critical, tr("Fehler im Auftrag"),
                    tr("GRBL meldet einen Fehler. Der Auftrag ist angehalten.\n\n%1\n\n"
                       "Die Zeile wurde von GRBL nicht ausgeführt. Setzt du fort, wird sie übersprungen: Alles danach "
                       "läuft ohne sie weiter, und das Ergebnis kann sich verschieben oder unvollständig sein.").arg(message),
                    QMessageBox::NoButton, dialogParent_);
    // Dieser Dialog ist modal und sperrt das Hauptfenster samt NOT-AUS-Knopf. Darum hat er einen eigenen NOT-AUS,
    // der auch auf Esc reagiert (planung.md 3.31: der Not-Aus muss immer erreichbar sein).
    QPushButton* abort = box.addButton(tr("Abbrechen (Reset, Laser aus)"), QMessageBox::DestructiveRole);
    QPushButton* proceed = box.addButton(tr("Zeile überspringen und fortsetzen"), QMessageBox::AcceptRole);
    QPushButton* emergency = box.addButton(tr("NOT-AUS (Esc)"), QMessageBox::RejectRole);
    emergency->setStyleSheet(QStringLiteral("QPushButton { background: #d42a29; color: white; font-weight: 600; }"));
    box.setDefaultButton(abort);
    box.setEscapeButton(emergency);
    box.exec();
    if (box.clickedButton() == proceed) {
        grbl_->continueJob();
    } else if (box.clickedButton() == abort) {
        cancelRequested_ = true;
        grbl_->cancelJob();
    } else {
        emergency_ = true;
        emit emergencyStopRequested();   // NOT-AUS-Knopf oder Esc (auch das Schließen des Fensters)
    }
}

}  // namespace ld::app
