#include "connectioncontroller.h"

#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include "appsettings.h"
#include "grbl/baudtester.h"
#include "portfactory.h"

namespace ld::app {

SelfTestResult evaluateSelfTest(const QMap<int, QString>& settings, const ld::core::MachineSetup& machine)
{
    SelfTestResult result;
    result.s30 = static_cast<int>(settings.value(30).toDouble());
    if (settings.contains(30) && result.s30 != machine.maxPower) {
        result.powerMismatch = true;
        result.problems << QObject::tr("GRBL meldet $30 = %1 (größtes S), das Maschinenprofil hat aber %2. "
                                       "Die Leistung im G-Code passt dann nicht zu GRBL.")
                               .arg(result.s30)
                               .arg(machine.maxPower);
    }
    result.laserMode = settings.value(32).toInt() == 1;
    if (settings.contains(32) && !result.laserMode && machine.laserCommand == ld::core::LaserCommand::M4) {
        result.laserModeMissing = true;
        result.problems << QObject::tr("Das Profil nutzt M4, GRBL steht aber auf $32 = 0 (Laser-Modus aus). "
                                       "M4 braucht den Laser-Modus ($32 = 1); sonst wird die Leistung nicht mit der Geschwindigkeit geregelt.");
    }
    return result;
}

ConnectionController::ConnectionController(const ld::core::MachineSetup& machine, QWidget* dialogParent, QObject* parent)
    : QObject(parent), machine_(machine), dialogParent_(dialogParent)
{
    using ld::grbl::GrblConnection;
    grbl_ = new GrblConnection(this);

    connect(grbl_, &GrblConnection::stateChanged, this, &ConnectionController::onStateChanged);
    connect(grbl_, &GrblConnection::infoRead, this, [this](const QString& version, const QString& options) {
        grblVersion_ = version;
        emit info(tr("GRBL-Version %1 (%2)").arg(version, options));
        emit versionChanged();
        reportVersion_ = version;
        reportOptions_ = options;
        gotInfo_ = true;
        collectReport();
    });
    connect(grbl_, &GrblConnection::parametersRead, this, [this](const QStringList& parameters) {
        if (gotParameters_) return;   // nur die Antwort auf die Abfrage direkt nach dem Verbinden
        reportParameters_ = parameters;
        gotParameters_ = true;
        collectReport();
    });
    connect(grbl_, &GrblConnection::settingsRead, this, [this](const QMap<int, QString>& settings) {
        if (selfTestPending_ && !gotSettings_) {
            reportSettings_ = settings;
            gotSettings_ = true;
            collectReport();
        }
        if (selfTestPending_) {
            selfTestPending_ = false;
            runSelfTest(settings);
        }
    });
    connect(grbl_, &GrblConnection::statusChanged, this, &ConnectionController::onStatus);
    // Kennt die Firmware "$#" nicht, kommt der Bericht trotzdem (ohne Parameter), damit er nicht ausbleibt.
    connect(grbl_, &GrblConnection::commandError, this, [this](int, const QString&, const QString& line) {
        if (line == QLatin1String("$#") && !gotParameters_) {
            gotParameters_ = true;
            collectReport();
        }
    });
    connect(grbl_, &GrblConnection::spindleLimited, this, [this](int requested, int limit) {
        const QString text = tr("Eine S-Angabe (%1) war größer als $30 (%2) und wurde auf %2 begrenzt. Maschinenprofil prüfen.")
                                 .arg(requested)
                                 .arg(limit);
        emit error(text);
        emit message(text, 8000);
    });
    connect(grbl_, &GrblConnection::commandError, this, [this](int code, const QString& text, const QString& line) {
        const QString msg = tr("Fehler %1 bei \"%2\": %3").arg(code).arg(line, text);
        emit error(msg);
        emit message(msg, 8000);
    });
    connect(grbl_, &GrblConnection::alarm, this, [this](int code, const QString& text, bool jobWasActive) {
        // Alarme werden immer angezeigt. Der Laser ist dabei schon aus (Reset ist gesendet).
        const QString msg = tr("ALARM %1: %2").arg(code).arg(text);
        emit error(msg);
        emit problem(tr("ALARM"), msg);
        showAlarmDialog(tr("%1\n\nDer Laser wurde ausgeschaltet%2.")
                            .arg(msg, jobWasActive ? tr(" und der Auftrag abgebrochen") : QString()));
    });
    connect(grbl_, &GrblConnection::connectFailed, this, [this](const QString& reason) {
        emit error(reason);
        QMessageBox::warning(dialogParent_, tr("Verbinden"), reason);
    });
    connect(grbl_, &GrblConnection::connectionLost, this, [this](const QString& reason) {
        const QString msg = tr("Die Verbindung zum Laser ist abgebrochen: %1").arg(reason);
        emit error(msg);
        emit problem(tr("Verbindung verloren"), tr("Die Verbindung zum Laser ist abgebrochen."));
        QMessageBox::critical(
            dialogParent_, tr("Verbindung verloren"),
            tr("%1\n\nVorsicht: GRBL fährt die bereits empfangenen Zeilen (bis zu etwa 15 Bewegungen) noch zu Ende "
               "und kann den Laser dabei eingeschaltet lassen. Es gibt keinen Hardware-Not-Aus: Bei Gefahr den Laser "
               "ausschalten oder das Netzteil abziehen.")
                .arg(msg));
    });
    connect(grbl_, &GrblConnection::unresponsive, this, [this](bool silent) {
        const QString msg = silent ? tr("GRBL antwortet nicht mehr.") : tr("GRBL antwortet wieder.");
        emit error(msg);
        if (silent) emit problem(tr("Laser antwortet nicht"), msg);
        emit message(msg, 8000);
    });
}

// ---------------------------------------------------------------------------------------------
// Verbinden
// ---------------------------------------------------------------------------------------------
void ConnectionController::connectTo(const QString& portKey, int baud)
{
    QSettings().setValue(QStringLiteral("connection/lastPort/") + machine_.name, portKey);
    grbl_->setPort(createPort(portKey, baud));
    QString err;
    if (!grbl_->connectDevice(&err)) {
        emit error(err);
        QMessageBox::warning(dialogParent_, tr("Verbinden"), err);
        return;
    }
    emit info(tr("Verbinde mit %1 (%2 Baud) …").arg(grbl_->port()->description()).arg(baud));
}

void ConnectionController::autoConnect()
{
    if (!AppSettings::instance().autoConnect() || grbl_->state() != ld::grbl::GrblConnection::State::Disconnected) return;
    const QString key = QSettings().value(QStringLiteral("connection/lastPort/") + machine_.name).toString();
    if (key.isEmpty() || key == simulatorKey()) return;   // der Simulator wird nie automatisch verbunden
    for (const PortChoice& choice : listPortChoices()) {
        if (choice.key == key) {
            emit info(tr("Auto-Verbindung mit %1 …").arg(choice.text));
            connectTo(key, machine_.baudRate);
            return;
        }
    }
    emit info(tr("Auto-Verbindung: Der Port %1 ist nicht vorhanden.").arg(key));
}

void ConnectionController::onStateChanged()
{
    if (!grbl_->isConnected()) {
        grblVersion_.clear();
        selfTestPending_ = false;
        emit versionChanged();
    } else {
        // Selbsttest: GRBL-Version und Einstellungen lesen und mit dem Maschinenprofil vergleichen.
        selfTestPending_ = true;
        gotInfo_ = gotSettings_ = gotParameters_ = reportSent_ = false;
        grbl_->readInfo();
        grbl_->readSettings();
        grbl_->readParameters();   // Koordinatensysteme ($#), für das Systemprotokoll
    }
    emit stateChanged();
}

// ---------------------------------------------------------------------------------------------
// Selbsttest
// ---------------------------------------------------------------------------------------------
// Sind Version, Einstellungen und Parameter alle da, geht der Bericht einmal hinaus (Systemprotokoll).
void ConnectionController::collectReport()
{
    if (reportSent_ || !gotInfo_ || !gotSettings_ || !gotParameters_) return;
    reportSent_ = true;
    emit firmwareReport(reportVersion_, reportOptions_, reportSettings_, reportParameters_);
}

void ConnectionController::runSelfTest(const QMap<int, QString>& settings)
{
    const SelfTestResult result = evaluateSelfTest(settings, machine_);
    if (result.passed()) {
        emit info(tr("Selbsttest bestanden: $30 = %1, Laser-Modus %2.").arg(result.s30).arg(result.laserMode ? tr("an") : tr("aus")));
        return;
    }
    for (const QString& problem : result.problems) emit error(tr("Selbsttest: ") + problem);
    QMessageBox box(QMessageBox::Warning, tr("Selbsttest der Verbindung"), result.problems.join(QStringLiteral("\n\n")),
                    QMessageBox::Ok, dialogParent_);
    QPushButton* fixProfile = result.powerMismatch ? box.addButton(tr("Profil auf $30 anpassen"), QMessageBox::ActionRole) : nullptr;
    QPushButton* openParams = result.laserModeMissing ? box.addButton(tr("GRBL-Parameter öffnen …"), QMessageBox::ActionRole) : nullptr;
    box.exec();
    if (fixProfile && box.clickedButton() == fixProfile) {
        emit profileMaxPowerRequested(result.s30);   // das Hauptfenster schreibt das Profil
    } else if (openParams && box.clickedButton() == openParams) {
        emit grblSettingsRequested();
    }
}

// ---------------------------------------------------------------------------------------------
// Baudrate testen
// ---------------------------------------------------------------------------------------------
void ConnectionController::runBaudTest(const QString& portKey)
{
    if (portKey == simulatorKey()) {
        QMessageBox::information(dialogParent_, tr("Baudrate testen"), tr("Der Simulator hat keine Baudrate."));
        return;
    }
    if (grbl_->state() != ld::grbl::GrblConnection::State::Disconnected) return;
    auto* tester = new ld::grbl::BaudTester([portKey](int baud) { return createPort(portKey, baud); },
                                            ld::grbl::BaudTester::commonBauds(), this);
    auto* progress = new QProgressDialog(tr("Teste Baudrate …"), tr("Abbrechen"), 0, 0, dialogParent_);
    progress->setWindowTitle(tr("Baudrate testen"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    connect(tester, &ld::grbl::BaudTester::testing, progress, [progress](int baud) {
        progress->setLabelText(QObject::tr("Teste %1 Baud … (GRBL braucht nach dem Öffnen einige Sekunden)").arg(baud));
    });
    connect(progress, &QProgressDialog::canceled, tester, &ld::grbl::BaudTester::cancel);
    connect(tester, &ld::grbl::BaudTester::failed, this, [this, progress, tester](const QString& reason) {
        progress->close();
        QMessageBox::warning(dialogParent_, tr("Baudrate testen"), reason);
        tester->deleteLater();
        progress->deleteLater();
    });
    connect(tester, &ld::grbl::BaudTester::finished, this, [this, progress, tester](int baud) {
        progress->close();
        if (baud > 0) {
            emit baudRateFound(baud);   // das Hauptfenster schreibt es ins Profil
            emit info(tr("GRBL antwortet bei %1 Baud.").arg(baud));
            QMessageBox::information(dialogParent_, tr("Baudrate testen"),
                                     tr("GRBL antwortet bei %1 Baud. Die Rate ist im Profil gespeichert.").arg(baud));
        } else if (!progress->wasCanceled()) {
            QMessageBox::warning(dialogParent_, tr("Baudrate testen"),
                                 tr("Bei keiner Baudrate kam eine Antwort. Bitte Kabel, Treiber (CH340/CH341), den gewählten Port "
                                    "und die Stromversorgung des Lasers prüfen. Unter Linux muss der Benutzer in der Gruppe dialout sein."));
        }
        tester->deleteLater();
        progress->deleteLater();
    });
    tester->start();
}

// ---------------------------------------------------------------------------------------------
// Nullpunkt
// ---------------------------------------------------------------------------------------------
void ConnectionController::setZero()
{
    if (!grbl_->sendLine(QStringLiteral("G10 L20 P1 X0 Y0"))) return;
    grbl_->confirmPosition();   // wer den Nullpunkt setzt, hat die Position geprüft
    emit message(tr("Nullpunkt gesetzt (X0 Y0)."), 5000);
}

void ConnectionController::goZero()
{
    grbl_->sendLine(QStringLiteral("M5"));
    grbl_->sendLine(QStringLiteral("G90 G0 X0 Y0"));
}

// ---------------------------------------------------------------------------------------------
// Alarm
// ---------------------------------------------------------------------------------------------
// Dialog bei einem Alarm. Der Bediener kann den Alarm gleich aufheben ($X) oder den Dialog schließen; später geht
// es über das Menü Maschine.
void ConnectionController::showAlarmDialog(const QString& text)
{
    if (alarmDialogOpen_) return;
    alarmDialogOpen_ = true;
    alarmDialogShown_ = true;
    QMessageBox box(QMessageBox::Critical, tr("Alarm von GRBL"),
                    tr("%1\n\nDie Position kann ungenau sein; bitte den Nullpunkt prüfen.").arg(text), QMessageBox::Ok, dialogParent_);
    QPushButton* unlock = box.addButton(tr("Alarm aufheben ($X)"), QMessageBox::ActionRole);
    box.exec();
    alarmDialogOpen_ = false;
    if (box.clickedButton() == unlock) confirmUnlock();
}

void ConnectionController::confirmUnlock()
{
    if (QMessageBox::question(dialogParent_, tr("Alarm aufheben"),
                              tr("Die Sperre nach dem Alarm wird mit $X aufgehoben. Danach kann sich der Laser wieder bewegen. "
                                 "Der Laser bleibt aus. Die Position kann ungenau sein; bitte den Nullpunkt prüfen.\n\nFortfahren?"),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
        grbl_->sendLine(QStringLiteral("$X"));
    }
}

// Statusmeldung: Menüpunkt "Alarm aufheben" und Dialog. Ein Alarm ohne eigene ALARM-Meldung (zum Beispiel gleich nach
// dem Einschalten) bekommt den Dialog hier; nach einem regulären Abbrechen hebt die Verbindung den Alarm selbst auf,
// dann gibt es keinen Dialog.
void ConnectionController::onStatus(const ld::grbl::GrblStatus& status)
{
    const bool inAlarm = status.state == ld::grbl::MachineState::Alarm;
    emit alarmStateChanged(inAlarm && grbl_->isConnected());
    if (!inAlarm) {
        alarmDialogShown_ = false;
        return;
    }
    if (alarmDialogShown_ || alarmDialogOpen_ || grbl_->autoUnlockPending()) return;
    alarmDialogShown_ = true;   // sofort merken, damit der verzögerte Aufruf nicht doppelt kommt
    QTimer::singleShot(0, this, [this] {
        if (grbl_->status().state == ld::grbl::MachineState::Alarm) {
            alarmDialogShown_ = false;   // showAlarmDialog setzt es wieder
            showAlarmDialog(tr("GRBL ist im Alarmzustand (der Laser ist aus)."));
        }
    });
}

}  // namespace ld::app
