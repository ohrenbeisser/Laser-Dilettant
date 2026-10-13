#pragma once

#include <QMetaType>
#include <QString>

#include <optional>

// Machine states as reported by GRBL, plus Disconnected/Connecting which are
// only used by the program itself.
enum class MachineState {
    Disconnected,
    Connecting,
    Unknown,
    Idle,
    Run,
    Hold,
    Jog,
    Alarm,
    Door,
    Check,
    Home,
    Sleep,
};

struct Vec3
{
    double x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

// Parsed GRBL status report (<...>). Optional fields are only set when they
// were part of the report; GRBL sends WCO and Ov only every few reports.
struct StatusReport
{
    MachineState state = MachineState::Unknown;
    int subState = -1;                // e.g. Hold:0 / Hold:1, Door:n
    QString stateText;                // raw state word
    std::optional<Vec3> mpos;         // machine position
    std::optional<Vec3> wpos;         // work position
    std::optional<Vec3> wco;          // work coordinate offset
    std::optional<double> feed;       // current feed (mm/min)
    std::optional<double> spindle;    // current S value
    std::optional<int> ovFeed, ovRapid, ovSpindle;
    std::optional<int> plannerFree, rxFree; // Bf:
    std::optional<int> lineNumber;    // Ln:
    QString pins;                     // Pn:
    QString accessories;              // A:
};

// Position and overrides with the last known values filled in.
struct MachineStatus
{
    MachineState state = MachineState::Disconnected;
    int subState = -1;
    Vec3 mpos;
    Vec3 wpos;
    Vec3 wco;
    double feed = 0;
    double spindle = 0;
    int ovFeed = 100, ovRapid = 100, ovSpindle = 100;
    QString pins;
};

struct ConnectionParams
{
    QString portName;      // empty or "simulator" for the built-in simulator
    int baudRate = 115200;
    bool simulator = false;
};

QString machineStateName(MachineState s);

Q_DECLARE_METATYPE(MachineState)
Q_DECLARE_METATYPE(MachineStatus)
