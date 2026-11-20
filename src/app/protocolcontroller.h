// ProtocolController: schreibt das Auftragsprotokoll und das Systemprotokoll (siehe docs/planung.md 3.38).
//
// Er hört auf JobController (Beginn und Ende eines Laufs) und ConnectionController (Verbinden, Alarme, Fehler,
// Verbindungsverlust) und legt dazu Zeilen in den CSV-Dateien an. Das MainWindow meldet nur, was sonst niemand sieht
// (Programm gestartet und beendet, NOT-AUS, Profilwechsel). Die Dateien selbst und die Statistik stecken in
// core/protocol, damit sie ohne Oberfläche testbar sind.
#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "core/machinesetup.h"
#include "core/protocol.h"
#include "jobcontroller.h"

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class ConnectionController;

class ProtocolController : public QObject {
    Q_OBJECT

public:
    // folder: Ordner der Protokolle. materialName: Name des gewählten Materials; ncFileName: Dateiname der geladenen
    // NC-Datei; ncMode: ist gerade eine NC-Datei geladen.
    ProtocolController(const QString& folder, JobController* jobs, ConnectionController* connection,
                       ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                       std::function<QString()> materialName, std::function<QString()> ncFileName,
                       std::function<bool()> ncMode, QObject* parent = nullptr);

    // Vorgabeordner der Protokolle (Programmdaten des Benutzers, nicht die Einstellungen).
    static QString defaultFolder();

    ld::core::JobLog& jobLog() { return jobLog_; }
    ld::core::SystemLog& systemLog() { return systemLog_; }
    const QString& folder() const { return folder_; }

    // Ein Ereignis ins Systemprotokoll.
    void log(ld::core::LogLevel level, const QString& category, const QString& message, const QString& details = QString());
    // Beim Programmstart: ein beim letzten Mal nicht beendeter Auftrag wird als "unterbrochen" markiert.
    void recoverAtStart();

signals:
    void changed();   // es wurde etwas protokolliert (ein offenes Protokollfenster kann sich auffrischen)

private:
    void onJobStarted(JobController::Kind kind);
    void onJobEnded(JobController::Kind kind, ld::core::JobResult result, const QString& note, double runSeconds,
                    double activeSeconds, int lines);
    QString jobName(JobController::Kind kind) const;
    QString kindText(JobController::Kind kind) const;

    QString folder_;
    JobController* jobs_;
    ConnectionController* connection_;
    ld::core::ImageDocument* document_;
    const ld::core::MachineSetup& machine_;
    std::function<QString()> materialName_;
    std::function<QString()> ncFileName_;
    std::function<bool()> ncMode_;
    ld::core::JobLog jobLog_;
    ld::core::SystemLog systemLog_;
    ld::core::JobRecord current_;   // der laufende Auftrag (id leer: keiner)
};

}  // namespace ld::app
