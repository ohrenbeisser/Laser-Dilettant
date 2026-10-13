#pragma once

#include "device/GrblTypes.h"

#include <QString>

namespace GrblStatusParser {

MachineState stateFromString(const QString& word);

// Parses "<Idle|MPos:1,2,3|FS:0,0|...>". Unknown fields are ignored.
// Returns false if the line is not a status report.
bool parse(const QString& line, StatusReport* out);

// Merges a report into the accumulated status. Missing WPos/MPos is computed
// from the (last known) WCO, as described in the GRBL interface docs.
void merge(const StatusReport& report, MachineStatus* status);

} // namespace GrblStatusParser
