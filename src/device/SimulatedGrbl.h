#pragma once

#include "device/GrblSettings.h"
#include "device/GrblTypes.h"
#include "device/ITransport.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTimer>

#include <deque>

// A virtual GRBL 1.1 controller. Implements ITransport, so the driver talks
// to it exactly like to a serial port. Used by the unit tests and to try the
// program without a laser ("Simulator" in the port list).
//
// Simulated:
//  - boot banner after opening (incoming bytes are dropped until then)
//  - 128 byte receive buffer, 15 block planner, "ok" when a line is accepted
//  - transfer time according to the simulated baud rate
//  - motion with feed rate and simple acceleration, feed hold, resume
//  - real-time commands ? ! ~ 0x18 0x85 and the overrides 0x90..0x9D
//  - $$, $#, $G, $I, $N, $X, $H, $C, $J=, $N=value, $RST
//  - G0 G1 G2 G3 (as straight lines) G4 G10 L2/L20 G17-19 G20 G21 G28 G30
//    G53 G54 G90 G91 G92 G93 G94, M0-M2 M3 M4 M5 M7-M9 M30, F S N P L
//  - laser mode ($32): laser fires with M3/M4 only during G1/G2/G3 motion
class SimulatedGrbl : public ITransport
{
    Q_OBJECT
public:
    explicit SimulatedGrbl(QObject* parent = nullptr);
    ~SimulatedGrbl() override;

    bool open(QString* errorMessage) override;
    void close() override;
    bool isOpen() const override { return m_open; }
    void write(const QByteArray& data) override;
    QString description() const override;

    // --- configuration --------------------------------------------------
    // Transfer speed in both directions; 0 = no delay.
    void setBaudRate(int baud) { m_baud = baud; }
    int baudRate() const { return m_baud; }
    // Motion runs this many times faster than real time (for tests).
    void setTimeScale(double scale) { m_timeScale = scale; }
    void setBootDelayMs(int ms) { m_bootDelayMs = ms; }
    void setBanner(const QString& banner) { m_banner = banner; }
    // false: opening the port does not reset the board (no banner until a
    // soft reset is received).
    void setResetOnOpen(bool on) { m_resetOnOpen = on; }
    // Answer the n-th G-code line (1-based, counted since open/reset) with
    // error:code instead of executing it.
    void injectError(int gcodeLineNumber, int code) { m_injectedErrors.insert(gcodeLineNumber, code); }
    // Stop answering anything (simulates a hanging controller).
    void setHung(bool hung) { m_hung = hung; }
    // Simulates pulling the USB cable.
    void simulateCableLoss();
    GrblSettings& settings() { return m_settings; }

    // --- inspection (tests) ---------------------------------------------
    Vec3 machinePosition() const { return m_mpos; }
    MachineState state() const { return m_state; }
    int maxRxUsage() const { return m_maxRxUsage; }
    bool rxOverflowed() const { return m_rxOverflow; }
    int executedGcodeLines() const { return m_gcodeLineCount; }
    const QStringList& receivedLines() const { return m_receivedLines; }
    bool laserFiring() const;
    double spindleValue() const { return m_spindleS; }
    int feedOverride() const { return m_ovFeed; }
    int spindleOverride() const { return m_ovSpindle; }
    int resetCount() const { return m_resetCount; }

private:
    enum class Spindle { Off, M3, M4 };

    struct Block
    {
        enum Kind { Move, Dwell, Home } kind = Move;
        Vec3 target;          // machine coordinates
        double feed = 0;      // mm/min; 0 = rapid
        bool rapid = true;
        bool jog = false;
        double dwellSeconds = 0;
        Spindle spindle = Spindle::Off;
        double s = 0;
    };

    void tick();
    void boot();
    void softReset();
    void handleRealtime(char c);
    void receiveByte(char c);
    void processLines();
    void send(const QString& text);
    void sendLine(const QString& line);
    void sendStatus();
    QString executeLine(const QString& line); // returns "ok" or "error:N"
    QString executeSystemCommand(const QString& line);
    QString executeGcode(const QString& line, bool jog);
    void advanceMotion(double dt);
    void loadDefaultSettings();
    bool plannerFull() const { return m_planner.size() >= kPlannerSize; }
    bool isMoving() const;

    static constexpr int kRxBufferSize = 128;
    static constexpr size_t kPlannerSize = 15;

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTick = 0;
    bool m_open = false;
    bool m_booted = false;
    bool m_hung = false;
    bool m_resetOnOpen = true;
    int m_bootDelayMs = 300;
    qint64 m_bootAt = 0;
    int m_baud = 0;
    double m_timeScale = 1.0;
    double m_inBudget = 0;
    double m_outBudget = 0;
    QString m_banner = QStringLiteral("Grbl 1.1h ['$' for help]");

    QByteArray m_inWire;   // bytes travelling to the controller
    QByteArray m_outWire;  // bytes travelling to the PC
    QByteArray m_rx;       // GRBL receive buffer
    int m_maxRxUsage = 0;
    bool m_rxOverflow = false;
    bool m_statusRequested = false;
    int m_statusCount = 0;
    bool m_wcoChanged = true;
    bool m_ovChanged = true;
    QStringList m_receivedLines;
    QHash<int, int> m_injectedErrors;
    int m_gcodeLineCount = 0;
    int m_resetCount = 0;

    GrblSettings m_settings;
    MachineState m_state = MachineState::Idle;
    int m_holdSub = 0;
    bool m_checkMode = false;
    bool m_homingPending = false;

    // Modal state
    bool m_absolute = true;
    bool m_inches = false;
    int m_motionMode = 0;
    double m_feed = 0;
    double m_spindleS = 0;
    Spindle m_spindle = Spindle::Off;
    int m_coolant = 9;

    Vec3 m_mpos;
    Vec3 m_wco;       // G54 offset
    Vec3 m_g92;       // G92 offset
    Vec3 m_plannedPos; // machine position at the end of the planner queue
    std::deque<Block> m_planner;
    double m_velocity = 0; // mm/min
    double m_dwellLeft = 0;
    bool m_feedHold = false;
    int m_ovFeed = 100, m_ovRapid = 100, m_ovSpindle = 100;
};
