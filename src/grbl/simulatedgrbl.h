// Simulierter GRBL-1.1-Controller hinter der Schnittstelle IPort (siehe planung.md 3.15).
//
// Er verhält sich wie ein Laser mit GRBL: Er hat einen Empfangspuffer von 127 Bytes und einen Planer
// für 15 Fahrblöcke, bestätigt Zeilen mit "ok" erst, wenn sie aus dem Puffer genommen werden, kennt
// Echtzeitbefehle (? ! ~ Ctrl-X, 0x85), Jog, G10 L20, $-Einstellungen, Alarme und Fehler und fährt
// die Wege in simulierter Zeit (Tempo einstellbar). Für Tests und als "Simulator (ohne Laser)".
#pragma once

#include <algorithm>

#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QTimer>
#include <deque>
#include <optional>

#include "grbl/port.h"

namespace ld::grbl {

class SimulatedGrbl : public IPort {
    Q_OBJECT

public:
    explicit SimulatedGrbl(QObject* parent = nullptr);

    bool open(QString* error) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void write(const QByteArray& data) override;
    QByteArray readAll() override;
    QString description() const override { return tr("Simulator (ohne Laser)"); }

    // ---- Einstellungen und Prüfhaken für Tests ----
    void setTimeScale(double factor) { timeScale_ = factor; }   // 1 = Echtzeit
    void setSilent(bool silent) { silent_ = silent; }           // antwortet nicht mehr
    void dropConnection();                                      // simuliert gezogenes USB-Kabel
    void injectAlarm(int code);
    void injectRaw(const QByteArray& bytes);                    // rohe Bytes an den Empfänger (Rauschen, falsche Baudrate)                                 // löst einen Alarm aus
    void failLine(int acceptedLineNumber, int errorCode);       // die n-te angenommene G-Code-Zeile liefert error:code
    void setSetting(int number, const QString& value) { settings_[number] = value; }

    QPointF position() const { return mpos_; }                  // Maschinenposition
    QPointF workOffset() const { return wco_; }
    bool laserOn() const { return laserOn_; }
    bool overflowed() const { return overflowed_; }             // mehr als 127 Bytes im Puffer (echtes GRBL würde Zeichen verlieren)
    int maxRxBytes() const { return maxRx_; }
    const QStringList& acceptedLines() const { return accepted_; }   // alle G-Code-Zeilen, die GRBL angenommen hat
    int resetCount() const { return resets_; }
    // Kleinster und größter Punkt, den der Kopf angefahren hat (Maschinenkoordinaten).
    QPointF minPosition() const { return minPos_; }
    QPointF maxPosition() const { return maxPos_; }
    // Größte Leistung (S), mit der der Laser beim Fahren tatsächlich an war.
    int maxLaserS() const { return maxLaserS_; }
    int feedOverride() const { return feedOv_; }
    int spindleOverride() const { return spindleOv_; }
    bool isMoving() const { return current_.has_value() || !planner_.isEmpty() || !jogs_.empty(); }

private:
    struct Block {
        bool spindleOnly = false;
        bool rapid = false;
        bool jog = false;
        QPointF from, to;
        double lengthMm = 0.0;
        double feed = 0.0;       // mm/min
        int mode = 0;            // 0 = M5, 3 = M3, 4 = M4
        int s = 0;
    };

    void tick();
    void feed(const QByteArray& bytes);
    void processLines();
    bool processLine(const QString& line);   // false: bleibt im Puffer (Planer voll oder noch nicht ausführbar)
    bool processGcode(const QString& line, bool jog);
    void advance(double seconds);
    void reply(const QString& text);
    void sendStatus();
    void reset(bool moving);
    void setAlarm(int code);
    QString stateText() const;
    bool isIdle() const { return !isMoving() && alarm_ == 0; }

    bool open_ = false;
    bool booted_ = false;
    QByteArray out_;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 lastTickNs_ = 0;
    double timeScale_ = 1.0;
    bool silent_ = false;

    // Empfang
    QByteArray partial_;
    std::deque<std::pair<QString, int>> rxLines_;   // Zeile und ihre Größe in Bytes
    int rxBytes_ = 0;
    bool overflowed_ = false;
    int maxRx_ = 0;

    // Maschine
    QMap<int, QString> settings_;
    QPointF mpos_, wco_;
    QPointF programmed_;       // programmierte Position (Maschinenkoordinaten) beim Einlesen
    bool relative_ = false;
    bool rapidMode_ = true;
    double feedRate_ = 0.0;
    int sValue_ = 0;
    int spindle_ = 0;
    QList<Block> planner_;
    std::deque<Block> jogs_;
    std::optional<Block> current_;
    double doneMm_ = 0.0;
    bool hold_ = false;
    double holdAge_ = 0.0;
    int alarm_ = 0;
    bool laserOn_ = false;
    int statusCounter_ = 0;
    int execMode_ = 0;
    int execS_ = 0;
    int acceptedCount_ = 0;
    bool notifyPending_ = false;

    QStringList accepted_;
    int failAt_ = -1;
    int failCode_ = 0;
    int resets_ = 0;
    QPointF minPos_, maxPos_;
    // S nach Override, wie im echten GRBL auf $30 begrenzt.
    int effectiveS() const { return std::min(execS_ * spindleOv_ / 100, settings_.value(30, QStringLiteral("1000")).toInt()); }
    int maxLaserS_ = 0;
    int feedOv_ = 100, spindleOv_ = 100;
};

}  // namespace ld::grbl
