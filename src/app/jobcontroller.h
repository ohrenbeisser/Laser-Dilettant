// JobController: Alles, was einen Lauf am Laser betrifft (zerlegt aus dem MainWindow, siehe planung.md 3.33).
//
// Er kennt vier Arten von Läufen (Kind): das Bild bzw. die NC-Datei (Image), den Rahmen (Frame), den Materialtest
// (MaterialTest) und "keinen" (None). Er
//   * stellt vor dem Start die Fragen (Zustand von GRBL, Position nach Not-Aus, Schutzbrille, Rahmen-Leistung),
//   * erzeugt und startet den G-Code über die Verbindung (GrblConnection) und schaltet das Schwachlicht vorher aus,
//   * wartet auf die Laserwege der Vorschau, wenn der Start gedrückt wurde, bevor sie fertig sind,
//   * meldet den Fortschritt, ordnet die Laserposition dem Weg zu und zeigt Fehler im Auftrag (mit NOT-AUS),
//   * meldet Beginn und Ende über Signale. Die Oberfläche (Arbeitsfläche, Statusleiste, Konsole, Schalter) bleibt im
//     MainWindow und reagiert nur auf diese Signale.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <functional>

#include "core/gcode.h"
#include "core/machinesetup.h"
#include "core/materialtest.h"
#include "core/protocol.h"

class QWidget;

namespace ld::core {
class ImageDocument;
class PathPreview;
}

namespace ld::grbl {
class GrblConnection;
class WeakLight;
struct GrblStatus;
}

namespace ld::app {

class JobController : public QObject {
    Q_OBJECT

public:
    enum class Kind {
        None,
        Image,          // das Bild oder die geladene NC-Datei
        Frame,          // der Rahmen
        MaterialTest,   // der Materialtest
    };

    // machine: das Maschinenprofil des Hauptfensters (die Referenz bleibt gültig, der Wert ändert sich mit dem Profil).
    // ncMode: fragt, ob gerade ein NC-Auftrag statt eines Bildes gezeigt wird.
    JobController(ld::grbl::GrblConnection* grbl, ld::grbl::WeakLight* weak, ld::core::PathPreview* preview,
                  ld::core::ImageDocument* document, const ld::core::MachineSetup& machine,
                  std::function<bool()> ncMode, QWidget* dialogParent, QObject* parent = nullptr);

    Kind kind() const { return kind_; }
    // Rahmen und Materialtest haben keine Zuordnung zum Bild (keine grüne Spur, keine Live-Regler für das Bild).
    bool isFrameLike() const { return kind_ == Kind::Frame || kind_ == Kind::MaterialTest; }
    bool isMaterialTest() const { return kind_ == Kind::MaterialTest; }
    // Bis hierher sind die Wegstücke am Laser erledigt (nur für Kind::Image).
    size_t progressIndex() const { return progressIndex_; }
    double elapsedSeconds() const { return timer_.elapsed() / 1000.0; }
    // Arbeitszeit des laufenden Auftrags: Zeit, in der GRBL im Zustand "Läuft" war (ohne Pausen).
    double activeSeconds() const { return activeMs_ / 1000.0; }
    // Geschätzte Dauer des laufenden oder zuletzt gestarteten Auftrags (Rahmen: 0).
    double estimateSeconds() const;
    // Der NOT-AUS wurde ausgelöst (Hauptfenster): Ein dadurch abgebrochener Auftrag endet mit dem Ergebnis NOT-AUS.
    void noteEmergencyStop() { emergency_ = true; }

    // --- Start ---
    // Start-Knopf: prüft, fragt und startet (sobald die Laserwege der Vorschau da sind). Der Aufrufer hat schon
    // geprüft, dass es einen Auftrag gibt und er in den Arbeitsbereich passt.
    void requestStart();
    // Der Abbruch-Knopf (Auftrag läuft): fragt nach und bricht ab (Reset und M5).
    void confirmCancel();
    // Abbruch aus der Ferne (Fernansicht): ohne Rückfrage am PC (die Seite fragt selbst); wie der Abbrechen-Knopf.
    void cancelFromRemote();
    // Rahmen um das Bild bzw. um alle Wege der NC-Datei.
    void startFrame(double speedMmPerMin, double powerPercent);
    // Rahmen um den Materialtest.
    void startMaterialTestFrame(const ld::core::MaterialTestSpec& spec, double speedMmPerMin, double powerPercent);
    void startMaterialTest(const ld::core::MaterialTestSpec& spec);

    // --- Vorschau ---
    bool startPending() const { return startPending_; }
    // Der Auftrag hat sich geändert: Ein wartender Start verfällt.
    void cancelPendingStart() { startPending_ = false; }
    // Die Laserwege sind fertig; true, wenn damit ein wartender Start ausgelöst wurde.
    bool onPreviewReady();
    // Die Laserwege sind fehlgeschlagen; true, wenn ein Start wartete (er verfällt, der Bediener bekommt eine Meldung).
    bool onPreviewFailed(const QString& error);

    // --- Laser ---
    // Neue Statusmeldung von GRBL: Zuordnung der Position zum Weg, Fortschritt, Zeitanzeige.
    void trackLaser(const ld::grbl::GrblStatus& status);

signals:
    void started(ld::app::JobController::Kind kind);
    void finished(ld::app::JobController::Kind kind);
    // Der Auftrag ist zu Ende (fertig oder abgebrochen), für das Protokoll: Ergebnis, Hinweis (zum Beispiel Fehlermeldung),
    // Laufzeit und Arbeitszeit in Sekunden und die von GRBL bestätigten Zeilen. Kommt vor finished() bzw. nach dem Abbruch.
    void ended(ld::app::JobController::Kind kind, ld::core::JobResult result, const QString& note, double runSeconds,
               double activeSeconds, int lines);
    void message(const QString& text, int milliseconds);   // Statusleiste
    void info(const QString& text);                        // Konsole
    void error(const QString& text);                       // Konsole, als Fehler
    void progress(int percent);                            // Fortschrittsbalken
    void durationText(const QString& text);                // Zeitanzeige während des Laufs
    void previewRequested();                               // die Wege werden jetzt erzeugt (Anzeige auffrischen)
    // Position des Lasers auf der Arbeitsfläche (Szene, mm). Läuft ein Bild: pathAdvanced, wenn der Weg vorrückt,
    // sonst headMoved; ohne Lauf: headIdle (das Fadenkreuz zeigt den Laser, wenn keine Simulation läuft).
    void pathAdvanced(size_t first, size_t last, const QPointF& head);
    void headMoved(const QPointF& head);
    void headIdle(const QPointF& head);
    void emergencyStopRequested();                         // NOT-AUS im Fehlerdialog

private:
    bool checkIdle(const QString& title);
    bool confirmPosition();
    bool confirmStartWarning();
    bool launch(const QString& title, const QString& path);   // schaltet Schwachlicht aus und startet die Datei
    void beginImageJob();
    void startFrameFromResult(const ld::core::FrameResult& frame);
    void onJobFinished();
    void onJobAborted(const QString& reason);
    void onJobError(long long lineNumber, int code, const QString& text, const QString& line);
    void endJob(ld::core::JobResult result, const QString& note);

    ld::grbl::GrblConnection* grbl_;
    ld::grbl::WeakLight* weak_;
    ld::core::PathPreview* preview_;
    ld::core::ImageDocument* document_;
    const ld::core::MachineSetup& machine_;
    std::function<bool()> ncMode_;
    QWidget* dialogParent_;

    Kind kind_ = Kind::None;
    bool startPending_ = false;      // Start gedrückt, die Wege werden noch erzeugt
    size_t progressIndex_ = 0;
    double testSeconds_ = 0.0;       // geschätzte Dauer des Materialtests
    QElapsedTimer timer_;
    QTemporaryDir dir_;              // Rahmen- und Materialtest-Dateien
    // Für das Protokoll
    qint64 activeMs_ = 0;            // Arbeitszeit (GRBL "Läuft")
    qint64 lastStatusMs_ = 0;
    bool lastWasRunning_ = false;
    bool cancelRequested_ = false;   // der Bediener hat den Abbruch bestätigt
    bool emergency_ = false;         // NOT-AUS
    int alarmCode_ = -1;
    QString errorNote_;              // letzte Fehlermeldung im Auftrag
    void resetProtocolState();
};

}  // namespace ld::app
