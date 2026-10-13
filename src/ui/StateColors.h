#pragma once

#include "device/GrblTypes.h"

#include <QColor>
#include <QString>

namespace StateColors {

// Background colour of the state indicator (section 4.2).
QColor background(MachineState s);
QColor foreground(MachineState s);
// Translated, user-visible name of a state.
QString displayName(MachineState s);

} // namespace StateColors
