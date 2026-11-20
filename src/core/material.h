// Materialprofil: gespeicherte Gravur-Werte für ein Material (siehe docs/planung.md 3.12).
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

#include "core/engraving.h"

namespace ld::core {

struct MaterialProfile {
    QString name;
    double speedMmPerMin = 1200.0;
    double powerPercent = 60.0;
    int passes = 1;
    // Werte für Schneiden (Cutting); speed/power/passes oben gelten für Gravieren (Linie, Full).
    double cutSpeedMmPerMin = 300.0;
    double cutPowerPercent = 100.0;
    int cutPasses = 1;
    double overscanMm = 1.0;
    bool bidirectional = true;
    double angleDeg = 0.0;
    QString note;

    friend bool operator==(const MaterialProfile&, const MaterialProfile&) = default;
};

// Material aus den aktuellen Gravur-Werten bzw. Werte eines Materials in die Operation übernehmen.
MaterialProfile materialFromOperation(const QString& name, const Operation& operation, const QString& note);
Operation applyMaterial(const Operation& base, const MaterialProfile& material);
// Stimmen die Gravur-Werte der Operation mit denen des Materials überein?
bool operationMatchesMaterial(const Operation& operation, const MaterialProfile& material);

QJsonObject materialToJson(const MaterialProfile& material);
std::optional<MaterialProfile> materialFromJson(const QJsonObject& json);

}  // namespace ld::core
