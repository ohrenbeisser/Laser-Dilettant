#include "core/material.h"

#include <algorithm>
#include <cmath>

namespace ld::core {

MaterialProfile materialFromOperation(const QString& name, const Operation& op, const QString& note)
{
    MaterialProfile m;
    m.name = name;
    const ModeValues engrave = engraveValuesOf(op);
    const ModeValues cut = cutValuesOf(op);
    m.speedMmPerMin = engrave.speedMmPerMin;
    m.powerPercent = engrave.powerPercent;
    m.passes = engrave.passes;
    m.cutSpeedMmPerMin = cut.speedMmPerMin;
    m.cutPowerPercent = cut.powerPercent;
    m.cutPasses = cut.passes;
    m.overscanMm = op.overscanMm;
    m.bidirectional = op.bidirectional;
    m.angleDeg = op.angleDeg;
    m.note = note;
    return m;
}

Operation applyMaterial(const Operation& base, const MaterialProfile& m)
{
    // Gravurart bleibt; beide Wertesätze kommen aus dem Material.
    Operation op = withModeValues(base, ModeValues{m.speedMmPerMin, m.powerPercent, m.passes},
                                  ModeValues{m.cutSpeedMmPerMin, m.cutPowerPercent, m.cutPasses});
    op.overscanMm = m.overscanMm;
    op.bidirectional = m.bidirectional;
    op.angleDeg = m.angleDeg;
    return op;
}

bool operationMatchesMaterial(const Operation& op, const MaterialProfile& m)
{
    // Die Oberfläche kennt Speed, Leistung und Richtung nur als ganze Zahlen.
    const auto same = [](const ModeValues& a, const ModeValues& b) {
        return std::lround(a.speedMmPerMin) == std::lround(b.speedMmPerMin) &&
               std::lround(a.powerPercent) == std::lround(b.powerPercent) && a.passes == b.passes;
    };
    return same(engraveValuesOf(op), ModeValues{m.speedMmPerMin, m.powerPercent, m.passes}) &&
           same(cutValuesOf(op), ModeValues{m.cutSpeedMmPerMin, m.cutPowerPercent, m.cutPasses}) &&
           std::abs(op.overscanMm - m.overscanMm) < 0.05 && op.bidirectional == m.bidirectional &&
           std::lround(op.angleDeg) == std::lround(m.angleDeg);
}

QJsonObject materialToJson(const MaterialProfile& m)
{
    QJsonObject o;
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = m.name;
    o[QStringLiteral("speedMmPerMin")] = m.speedMmPerMin;
    o[QStringLiteral("powerPercent")] = m.powerPercent;
    o[QStringLiteral("passes")] = m.passes;
    o[QStringLiteral("cutSpeedMmPerMin")] = m.cutSpeedMmPerMin;
    o[QStringLiteral("cutPowerPercent")] = m.cutPowerPercent;
    o[QStringLiteral("cutPasses")] = m.cutPasses;
    o[QStringLiteral("overscanMm")] = m.overscanMm;
    o[QStringLiteral("bidirectional")] = m.bidirectional;
    o[QStringLiteral("angleDeg")] = m.angleDeg;
    o[QStringLiteral("note")] = m.note;
    return o;
}

std::optional<MaterialProfile> materialFromJson(const QJsonObject& o)
{
    const QString name = o.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || o.value(QStringLiteral("version")).toInt(1) > 1) {
        return std::nullopt;
    }
    const MaterialProfile d;
    MaterialProfile m;
    m.name = name;
    m.speedMmPerMin = std::clamp(o.value(QStringLiteral("speedMmPerMin")).toDouble(d.speedMmPerMin), 1.0, 100000.0);
    m.powerPercent = std::clamp(o.value(QStringLiteral("powerPercent")).toDouble(d.powerPercent), 0.0, 100.0);
    m.passes = std::clamp(o.value(QStringLiteral("passes")).toInt(d.passes), 1, 99);
    m.cutSpeedMmPerMin = std::clamp(o.value(QStringLiteral("cutSpeedMmPerMin")).toDouble(d.cutSpeedMmPerMin), 1.0, 100000.0);
    m.cutPowerPercent = std::clamp(o.value(QStringLiteral("cutPowerPercent")).toDouble(d.cutPowerPercent), 0.0, 100.0);
    m.cutPasses = std::clamp(o.value(QStringLiteral("cutPasses")).toInt(d.cutPasses), 1, 99);
    m.overscanMm = std::clamp(o.value(QStringLiteral("overscanMm")).toDouble(d.overscanMm), 0.0, 100.0);
    m.bidirectional = o.value(QStringLiteral("bidirectional")).toBool(d.bidirectional);
    m.angleDeg = std::clamp(o.value(QStringLiteral("angleDeg")).toDouble(d.angleDeg), 0.0, 180.0);
    m.note = o.value(QStringLiteral("note")).toString();
    return m;
}

}  // namespace ld::core
