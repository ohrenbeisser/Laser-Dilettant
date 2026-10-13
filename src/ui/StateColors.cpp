#include "ui/StateColors.h"

#include <QCoreApplication>

namespace StateColors {

QColor background(MachineState s)
{
    switch (s) {
    case MachineState::Idle: return QColor(0x2e, 0x7d, 0x32);  // green
    case MachineState::Run:
    case MachineState::Jog:
    case MachineState::Home: return QColor(0x15, 0x65, 0xc0);  // blue
    case MachineState::Hold: return QColor(0xf9, 0xa8, 0x25);  // yellow
    case MachineState::Alarm:
    case MachineState::Door: return QColor(0xc6, 0x28, 0x28);  // red
    case MachineState::Connecting: return QColor(0x61, 0x61, 0x61);
    case MachineState::Disconnected:
    case MachineState::Check:
    case MachineState::Sleep:
    case MachineState::Unknown: break;
    }
    return QColor(0x75, 0x75, 0x75); // grey
}

QColor foreground(MachineState s)
{
    return s == MachineState::Hold ? QColor(Qt::black) : QColor(Qt::white);
}

QString displayName(MachineState s)
{
    switch (s) {
    case MachineState::Disconnected: return QCoreApplication::translate("StateColors", "Disconnected");
    case MachineState::Connecting: return QCoreApplication::translate("StateColors", "Connecting…");
    case MachineState::Unknown: return QCoreApplication::translate("StateColors", "Unknown");
    case MachineState::Idle: return QCoreApplication::translate("StateColors", "Idle");
    case MachineState::Run: return QCoreApplication::translate("StateColors", "Run");
    case MachineState::Hold: return QCoreApplication::translate("StateColors", "Hold");
    case MachineState::Jog: return QCoreApplication::translate("StateColors", "Jog");
    case MachineState::Alarm: return QCoreApplication::translate("StateColors", "Alarm");
    case MachineState::Door: return QCoreApplication::translate("StateColors", "Door");
    case MachineState::Check: return QCoreApplication::translate("StateColors", "Check");
    case MachineState::Home: return QCoreApplication::translate("StateColors", "Homing");
    case MachineState::Sleep: return QCoreApplication::translate("StateColors", "Sleep");
    }
    return {};
}

} // namespace StateColors
