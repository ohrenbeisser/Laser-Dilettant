// Projektdatei (.ldproj): Ein Auftrag mit Bild, Bildbearbeitung, Größe und Gravur-Werten
// (siehe docs/planung.md 3.13). JSON mit Versionsnummer.
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

#include "core/engraving.h"
#include "core/imagesettings.h"
#include "core/objecttransform.h"
#include "core/calibration.h"
#include "core/textmodel.h"

namespace ld::core {

struct ProjectData {
    std::optional<CalibrationSpec> calibration;   // Bild ist die Kalibrierungsgrafik
    std::optional<TextSpec> text;   // Bild ist ein Text (dann gibt es keinen Bildpfad)
    QString imagePath;              // absoluter Pfad des Bildes (leer: noch keine Datei, siehe Speichern)
    ObjectTransform transform;
    ImageSettings settings;
    Operation operation;
    JobOrigin origin = JobOrigin::TopLeft;
    QString materialName;           // nur zur Info
};

// JSON. projectFile bestimmt, wie der Bildpfad gespeichert wird: relativ zum Ordner der Projektdatei
// ("imagePath") und zusätzlich absolut ("imagePathAbsolute") als Rückfall.
QJsonObject projectToJson(const ProjectData& project, const QString& projectFile);

// Liest ein Projekt. Beim Bildpfad gilt: erst der relative Pfad (vom Ordner der Projektdatei aus),
// dann der absolute; gibt es keinen von beiden, bleibt imagePath leer, und missingImage (falls gegeben)
// enthält den zuletzt gespeicherten Pfad. Ungültige Werte werden korrigiert. Bei Fehlern (kein Objekt,
// zu neue Version, Größe 0) gibt es nichts zurück und error sagt warum.
std::optional<ProjectData> projectFromJson(const QJsonObject& json, const QString& projectFile,
                                           QString* error = nullptr, QString* missingImage = nullptr);

bool saveProjectFile(const QString& path, const ProjectData& project, QString* error = nullptr);
std::optional<ProjectData> loadProjectFile(const QString& path, QString* error = nullptr,
                                           QString* missingImage = nullptr);

}  // namespace ld::core
