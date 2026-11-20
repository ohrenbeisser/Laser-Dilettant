#include "core/engraving.h"

namespace ld::core {

Operation switchMode(const Operation& operation, EngraveMode mode)
{
    Operation result = operation;
    result.mode = mode;
    if (isCutMode(mode) != isCutMode(operation.mode)) {
        // Gruppe gewechselt: die aktiven Werte gegen die der anderen Gruppe tauschen.
        const ModeValues active{operation.speedMmPerMin, operation.powerPercent, operation.passes};
        result.speedMmPerMin = operation.otherValues.speedMmPerMin;
        result.powerPercent = operation.otherValues.powerPercent;
        result.passes = operation.otherValues.passes;
        result.otherValues = active;
    }
    return result;
}

ModeValues engraveValuesOf(const Operation& operation)
{
    return isCutMode(operation.mode)
               ? operation.otherValues
               : ModeValues{operation.speedMmPerMin, operation.powerPercent, operation.passes};
}

ModeValues cutValuesOf(const Operation& operation)
{
    return isCutMode(operation.mode)
               ? ModeValues{operation.speedMmPerMin, operation.powerPercent, operation.passes}
               : operation.otherValues;
}

Operation withModeValues(const Operation& operation, const ModeValues& engrave, const ModeValues& cut)
{
    Operation result = operation;
    const ModeValues& active = isCutMode(operation.mode) ? cut : engrave;
    const ModeValues& other = isCutMode(operation.mode) ? engrave : cut;
    result.speedMmPerMin = active.speedMmPerMin;
    result.powerPercent = active.powerPercent;
    result.passes = active.passes;
    result.otherValues = other;
    return result;
}

}  // namespace ld::core
