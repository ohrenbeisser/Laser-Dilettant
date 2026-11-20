// Verbindung zu einem GRBL-1.1-Controller (siehe docs/planung.md 3.15 und 9.5).
//
// Die Klasse
//   * sendet Zeilen mit GRBL-Zeichenzählung (nie mehr als kRxLimit Bytes unbestätigt unterwegs),
//   * schickt Echtzeitbefehle (? ! ~ Ctrl-X, Jog-Abbruch) sofort und an der Warteschlange vorbei,
//   * fragt den Status mehrmals pro Sekunde ab und wertet Antworten, Fehler und Alarme aus,
//   * streamt einen Auftrag aus einer Datei und hält bei Fehlern und Alarmen sofort an.
// Alles läuft im Hauptthread; die Ein-/Ausgabe über IPort ist asynchron.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QFile>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <deque>

#include "grbl/clock.h"
#include "grbl/grblparser.h"
#include "grbl/port.h"

namespace ld::grbl {

class GrblConnection : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected };

    // GRBL hat 128 Bytes Empfangspuffer (127 nutzbar). Mit Sicherheitsabstand.
    static constexpr int kRxLimit = 120;
    // Kommt von GRBL so viel ohne Zeilenende an (falsche Baudrate, Rauschen), wird der Empfangspuffer verworfen.
    static constexpr int kMaxLineBytes = 64 * 1024;

    explicit GrblConnection(QObject* parent = nullptr);
    ~GrblConnection() override;

    // Der Port gehört danach der Verbindung. Ein vorheriger wird (getrennt und) gelöscht.
    void setPort(IPort* port);
    IPort* port() const { return port_; }

    // Öffnet den Port; "verbunden" ist die Verbindung, sobald GRBL antwortet (Begrüßung oder Status).
    bool connectDevice(QString* error = nullptr);
    // Trennt sicher: Soft-Reset, M5, dann Port schließen.
    void disconnectDevice();

    State state() const { return state_; }
    bool isConnected() const { return state_ == State::Connected; }
    const GrblStatus& status() const { return status_; }
    QPointF workOffset() const { return wco_; }

    // Eine einzelne Zeile (Konsole, Jog, Nullpunkt ...). false, wenn nicht verbunden oder ein
    // Auftrag läuft (dann ist die Eingabe gesperrt).
    bool sendLine(const QString& line);

    // Liest die Einstellungen ($$) bzw. die Versionsinfo ($I); das Ergebnis kommt als Signal.
    // Geht nur, wenn verbunden und kein Auftrag läuft.
    bool readSettings();
    bool readInfo();
    // Liest die Koordinatensysteme und Versätze ($#); das Ergebnis kommt als Signal (je Zeile "G54:0.000,0.000,0.000").
    bool readParameters();

    // Live-Regler (GRBL-Overrides): Sollwerte in Prozent. GRBL kennt nur Schritte (+-10 %, +-1 %); die App
    // sendet die nötigen Schritte. Vorschub 10..200 %, Leistung 10..100 % (nie über den programmierten Wert).
    int feedOverride() const { return feedTarget_; }
    int spindleOverride() const { return spindleTarget_; }
    void setFeedOverride(int percent);
    void setSpindleOverride(int percent);
    void resetOverrides();   // beide zurück auf 100 %

    // Zeilen, die noch warten oder von GRBL noch nicht bestätigt sind (das Schwachlicht hält damit seinen Vorrat klein).
    int pendingLines() const { return static_cast<int>(commands_.size() + inFlight_.size()); }

    // Verwirft Zeilen, die noch nicht gesendet wurden (zum Beispiel wartende Jog-Schritte).
    void dropQueuedCommands() { commands_.clear(); }

    // Echtzeitbefehle: wirken sofort.
    void feedHold();     // !
    void resume();       // ~
    void jogCancel();    // 0x85
    void softReset();    // Ctrl-X, bricht auch einen Auftrag ab

    // Not-Aus: Soft-Reset, Warteschlange und Auftrag verwerfen, danach M5.
    void emergencyStop();
    // True nach einem Not-Aus oder Alarm, bis die Position wieder bestätigt wurde.
    bool positionUncertain() const { return positionUncertain_; }
    // Nach einem regulären Abbrechen (cancelJob) hebt die Verbindung den Alarm, den GRBL nach dem Reset in der
    // Fahrt meldet, selbst auf ($X). Solange das aussteht, ist der Alarm kein Grund für eine Meldung.
    bool autoUnlockPending() const;
    void confirmPosition() { positionUncertain_ = false; }

    // Auftrag aus einer G-Code-Datei streamen.
    bool startJob(const QString& path, QString* error = nullptr);
    bool isJobActive() const { return jobActive_; }
    bool isJobHeld() const { return jobHeld_; }
    // Nach einem Fehler "trotzdem fortsetzen" bzw. abbrechen (Reset und M5).
    void continueJob();
    void cancelJob();
    double jobFraction() const;   // 0..1, nach gelesenen Bytes der Datei
    long long jobLinesAcknowledged() const { return jobAcked_; }

    // Größtes S, das GRBL kennt ($30; 0 = noch nicht gelesen). Jede S-Angabe, die gesendet wird, wird darauf begrenzt.
    int maxSpindle() const { return maxSpindle_; }
    // Bytes im Empfangspuffer ohne vollständige Zeile (für Tests).
    int receiveBacklogBytes() const { return static_cast<int>(rxBuffer_.size()); }

signals:
    void overridesChanged(int feedPercent, int spindlePercent);
    void resetSent();                                               // Ein Soft-Reset wurde gesendet (der Laser ist danach aus)
    void settingsRead(const QMap<int, QString>& settings);          // Antwort auf $$ (Nummer -> Wert als Text)
    void infoRead(const QString& version, const QString& options);  // Antwort auf $I
    void parametersRead(const QStringList& parameters);              // Antwort auf $#: G54 bis G59, G28, G30, G92, TLO, PRB
    void stateChanged(ld::grbl::GrblConnection::State state);
    void statusChanged(const ld::grbl::GrblStatus& status);
    void lineReceived(const QString& line);                  // jede Zeile von GRBL (auch Status)
    void lineSent(const QString& line);                      // nur einzeln gesendete Zeilen (nicht der Auftrag)
    void welcome(const QString& text);                       // "Grbl 1.1h ['$' for help]"
    void commandError(int code, const QString& text, const QString& line);
    void alarm(int code, const QString& text, bool jobWasActive);
    void jobProgress(double fraction);
    void jobError(long long lineNumber, int code, const QString& text, const QString& line);
    void jobFinished();
    // Eine S-Angabe war größer als $30 und wurde auf $30 begrenzt (einmal je Verbindung und Auftrag gemeldet).
    void spindleLimited(int requested, int limit);
    void jobAborted(const QString& reason);
    void connectionLost(const QString& reason);
    void unresponsive(bool silent);                          // GRBL meldet sich nicht mehr / wieder
    void connectFailed(const QString& reason);

private:
    struct InFlight {
        int length;          // Bytes samt Zeilenende
        long long jobLine;   // Zeilennummer im Auftrag, sonst -1
        QString text;
    };

    void onReadyRead();
    void handleLine(const QString& line);
    void handleAck(bool ok, int code);
    void handleAlarm(int code);
    void onWelcome(const QString& text);
    void pump();
    bool nextJobLine(QString& line, long long& number);
    void sendRealtime(char byte);
    void setState(State state);
    void pollTick();
    void clearTransfers();
    void finishAbort(const QString& reason);
    void onPortFailed(const QString& reason);
    void learnSetting(int number, const QString& value);

    IPort* port_ = nullptr;
    State state_ = State::Disconnected;
    GrblStatus status_;
    QPointF wco_;
    QMap<int, QString> collectedSettings_;
    QString versionLine_, optionsLine_;
    QStringList collectedParams_;
    QByteArray rxBuffer_;
    std::deque<InFlight> inFlight_;
    int inFlightBytes_ = 0;
    std::deque<QString> commands_;       // einzelne Zeilen, die noch gesendet werden müssen

    // Auftrag
    bool jobActive_ = false;
    bool jobHeld_ = false;               // wegen eines Fehlers angehalten
    bool jobAllSent_ = false;
    QFile jobFile_;
    qint64 jobSize_ = 0;
    long long jobLineNumber_ = 0;
    long long jobAcked_ = 0;
    QString jobPendingLine_;
    long long jobPendingNumber_ = 0;
    bool hasJobPending_ = false;

    int maxSpindle_ = 0;                 // $30, 0 = unbekannt
    bool spindleLimitReported_ = false;
    bool positionUncertain_ = false;
    bool m5AfterReset_ = false;
    bool silent_ = false;
    QTimer pollTimer_;
    qint64 lastResponseMs_ = 0;
    qint64 connectStartMs_ = 0;
    qint64 selfResetMs_ = kLongAgoMs;   // Zeiten kommen aus monotonicMs() (grbl/clock.h)
    qint64 autoUnlockUntilMs_ = kLongAgoMs;   // bis dahin gilt ein Alarm nach cancelJob() als selbst gewollt

    // Overrides: Sollwert und das, was GRBL nach unseren gesendeten Schritten haben müsste.
    int feedTarget_ = 100, spindleTarget_ = 100;
    int feedApplied_ = 100, spindleApplied_ = 100;
    qint64 lastOverrideSendMs_ = kLongAgoMs;
    void sendOverrideSteps(int from, int to, bool spindle);
    void reconcileOverrides(const GrblStatus& status);
};

}  // namespace ld::grbl
