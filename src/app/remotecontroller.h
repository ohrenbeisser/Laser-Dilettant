// RemoteController: verbindet die Fernansicht (WebServer) mit dem Programm (siehe docs/planung.md 3.41).
//
// Er liefert dem Server den Zustand des Auftrags (Fortschritt, Zeiten, Regler) und führt die Befehle aus (Pause, Weiter,
// Abbrechen, Speed und Leistung). Die Einstellungen (an/aus, Port, Passwort, Fernsteuerung erlaubt) liest er aus AppSettings.
// Zugriffe (Anmeldungen, Befehle) kommen ins Systemprotokoll (Kategorie "Zugriff"). Standardmäßig ist alles aus.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <functional>

#include "core/protocol.h"
#include "jobcontroller.h"
#include "webserver.h"

namespace ld::app {

class ConnectionController;
class ProtocolController;

class RemoteController : public QObject, public WebBackend {
    Q_OBJECT

public:
    RemoteController(JobController* jobs, ConnectionController* connection, ProtocolController* protocol, QObject* parent = nullptr);

    // Liest die Einstellungen und startet oder stoppt den Server (nach dem Programmstart und nach den Einstellungen).
    void applySettings();
    // Woher das Bild des Gebrannten kommt (vom MainWindow gesetzt): gibt es eins, seine Versionsnummer und das Rendern
    // (längste Seite in Pixeln).
    struct PreviewSource {
        std::function<bool()> available;
        std::function<quint64()> revision;
        std::function<QImage(int)> render;
    };
    void setPreviewSource(PreviewSource source) { previewSource_ = std::move(source); }
    // Die Startwerte des Auftrags (= 100 % der Regler), damit die Seite dieselben Werte zeigt wie die Regler im Programm.
    struct LiveBase {
        bool valid = false;
        bool percentOnly = false;       // NC-Auftrag: die Regler im Programm zeigen nur Prozent
        double speedMmPerMin = 0.0;
        double powerPercent = 0.0;
        // die Regler im Programm, genau so: Wert und Bereich (Speed in mm/min, im NC-Modus Prozent; Leistung in Prozent)
        int speedValue = 0, speedMin = 0, speedMax = 0;
        int powerValue = 0, powerMin = 0, powerMax = 0;
    };
    void setLiveBaseSource(std::function<LiveBase()> source) { liveBaseSource_ = std::move(source); }
    bool isRunning() const { return server_.isListening(); }
    quint16 port() const { return server_.port(); }
    // Letzter Fehler beim Starten (zum Beispiel Port belegt), leer wenn alles gut ist.
    const QString& lastError() const { return lastError_; }

    // --- WebBackend ---
    bool hasPassword() const override;
    bool checkPassword(const QString& password) override;
    QJsonObject statusJson() override;
    bool controlAllowed() const override;
    QString execute(const QString& command, int value) override;
    QByteArray previewPng() override;
    void audit(const QString& event, const QString& peer, const QString& details) override;

signals:
    // Etwas wurde aus der Ferne getan oder jemand hat sich angemeldet (Hinweis in der Statusleiste).
    void activity(const QString& text);

private:
    QString stateName(QString* message) const;

    JobController* jobs_;
    ConnectionController* connection_;
    ProtocolController* protocol_;
    WebServer server_;
    QString lastError_;
    // Merkt sich das Ende des letzten Auftrags, damit die Seite "Fertig" oder den Grund eines Abbruchs zeigen kann.
    ld::core::JobResult lastResult_ = ld::core::JobResult::Running;   // Running = noch keiner
    JobController::Kind lastKind_ = JobController::Kind::None;
    double lastRunSeconds_ = 0.0;
    double lastEstimateSeconds_ = 0.0;
    QString lastProblem_;
    PreviewSource previewSource_;
    std::function<LiveBase()> liveBaseSource_;
    QByteArray cachedPng_;
    quint64 cachedRevision_ = 0;
    bool cacheValid_ = false;
};

}  // namespace ld::app
