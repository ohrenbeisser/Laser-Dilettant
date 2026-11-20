#include "core/project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace ld::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

const char* methodName(ConversionMethod m)
{
    switch (m) {
    case ConversionMethod::Threshold: return "threshold";
    case ConversionMethod::FloydSteinberg: return "floydSteinberg";
    case ConversionMethod::Atkinson: return "atkinson";
    case ConversionMethod::Bayer: return "bayer";
    case ConversionMethod::Grayscale: return "grayscale";
    }
    return "threshold";
}

ConversionMethod methodFromName(const QString& name)
{
    if (name == QLatin1String("floydSteinberg")) return ConversionMethod::FloydSteinberg;
    if (name == QLatin1String("atkinson")) return ConversionMethod::Atkinson;
    if (name == QLatin1String("bayer")) return ConversionMethod::Bayer;
    if (name == QLatin1String("grayscale")) return ConversionMethod::Grayscale;
    return ConversionMethod::Threshold;
}

}  // namespace

QJsonObject projectToJson(const ProjectData& p, const QString& projectFile)
{
    QJsonObject o;
    o[QStringLiteral("version")] = 1;

    // Bild: relativ zum Ordner der Projektdatei (mit "/" als Trenner) und absolut als Rückfall.
    if (p.calibration) {
        o[QStringLiteral("calibration")] = calibrationSpecToJson(*p.calibration);
    } else if (p.text) {
        o[QStringLiteral("text")] = textSpecToJson(*p.text);
    } else if (!p.imagePath.isEmpty()) {
        const QDir projectDir = QFileInfo(projectFile).absoluteDir();
        o[QStringLiteral("imagePath")] = QDir::fromNativeSeparators(projectDir.relativeFilePath(p.imagePath));
        o[QStringLiteral("imagePathAbsolute")] = QDir::fromNativeSeparators(QFileInfo(p.imagePath).absoluteFilePath());
    }

    QJsonObject t;
    t[QStringLiteral("widthMm")] = p.transform.widthMm;
    t[QStringLiteral("heightMm")] = p.transform.heightMm;
    t[QStringLiteral("rotationDeg")] = p.transform.rotationDeg;
    t[QStringLiteral("flipHorizontal")] = p.transform.flipHorizontal;
    t[QStringLiteral("flipVertical")] = p.transform.flipVertical;
    t[QStringLiteral("offsetXMm")] = p.transform.offsetXMm;
    t[QStringLiteral("offsetYMm")] = p.transform.offsetYMm;
    o[QStringLiteral("transform")] = t;

    QJsonObject s;
    s[QStringLiteral("method")] = QString::fromLatin1(methodName(p.settings.method));
    s[QStringLiteral("autoThreshold")] = p.settings.autoThreshold;
    s[QStringLiteral("threshold")] = p.settings.threshold;
    s[QStringLiteral("brightness")] = p.settings.brightness;
    s[QStringLiteral("contrast")] = p.settings.contrast;
    s[QStringLiteral("gamma")] = p.settings.gamma;
    s[QStringLiteral("sharpen")] = p.settings.sharpen;
    s[QStringLiteral("invert")] = p.settings.invert;
    s[QStringLiteral("ditherCell")] = p.settings.ditherCell;
    s[QStringLiteral("pixelSizeMm")] = p.settings.pixelSizeMm;
    s[QStringLiteral("grayMinPercent")] = p.settings.grayPower.minPercent;
    s[QStringLiteral("grayMaxPercent")] = p.settings.grayPower.maxPercent;
    s[QStringLiteral("grayLevels")] = p.settings.grayPower.levels;
    s[QStringLiteral("svgFullStrokes")] = p.settings.svg.fullStrokes;
    s[QStringLiteral("svgCutStrokes")] = p.settings.svg.cutStrokes;
    s[QStringLiteral("svgCutFills")] = p.settings.svg.cutFills;
    s[QStringLiteral("vectorTurdSize")] = p.settings.vectorize.turdSize;
    s[QStringLiteral("vectorAlphaMax")] = p.settings.vectorize.alphaMax;
    s[QStringLiteral("vectorOptTolerance")] = p.settings.vectorize.optTolerance;
    if (!p.settings.crop.isEmpty()) {
        s[QStringLiteral("crop")] = QJsonArray{p.settings.crop.x(), p.settings.crop.y(), p.settings.crop.width(),
                                               p.settings.crop.height()};
    }
    o[QStringLiteral("imageSettings")] = s;

    QJsonObject op;
    op[QStringLiteral("mode")] = static_cast<int>(p.operation.mode);
    op[QStringLiteral("angleDeg")] = p.operation.angleDeg;
    op[QStringLiteral("speedMmPerMin")] = p.operation.speedMmPerMin;
    op[QStringLiteral("powerPercent")] = p.operation.powerPercent;
    op[QStringLiteral("passes")] = p.operation.passes;
    op[QStringLiteral("overscanMm")] = p.operation.overscanMm;
    op[QStringLiteral("bidirectional")] = p.operation.bidirectional;
    op[QStringLiteral("toleranceMm")] = p.operation.toleranceMm;
    op[QStringLiteral("otherSpeedMmPerMin")] = p.operation.otherValues.speedMmPerMin;
    op[QStringLiteral("otherPowerPercent")] = p.operation.otherValues.powerPercent;
    op[QStringLiteral("otherPasses")] = p.operation.otherValues.passes;
    o[QStringLiteral("operation")] = op;

    o[QStringLiteral("jobOrigin")] = static_cast<int>(p.origin);
    o[QStringLiteral("materialName")] = p.materialName;
    return o;
}

std::optional<ProjectData> projectFromJson(const QJsonObject& o, const QString& projectFile, QString* error,
                                           QString* missingImage)
{
    auto fail = [&](const QString& message) -> std::optional<ProjectData> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (o.isEmpty()) {
        return fail(tr("Die Datei enthält kein Projekt."));
    }
    if (o.value(QStringLiteral("version")).toInt(1) > 1) {
        return fail(tr("Das Projekt stammt aus einer neueren Version von Laser-Dilettant."));
    }

    ProjectData p;
    if (o.contains(QStringLiteral("calibration"))) {
        p.calibration = calibrationSpecFromJson(o.value(QStringLiteral("calibration")).toObject());
    } else if (o.contains(QStringLiteral("text"))) {
        p.text = textSpecFromJson(o.value(QStringLiteral("text")).toObject());
    }
    // Bildpfad: erst relativ, dann absolut.
    const QString relative = o.value(QStringLiteral("imagePath")).toString();
    const QString absolute = o.value(QStringLiteral("imagePathAbsolute")).toString();
    const QDir projectDir = QFileInfo(projectFile).absoluteDir();
    const QString fromRelative = relative.isEmpty() ? QString() : projectDir.absoluteFilePath(relative);
    if (p.text || p.calibration) {
        // Text- oder Kalibrierungs-Auftrag: kein Bildpfad nötig
    } else if (!fromRelative.isEmpty() && QFileInfo::exists(fromRelative)) {
        p.imagePath = QDir::cleanPath(fromRelative);
    } else if (!absolute.isEmpty() && QFileInfo::exists(absolute)) {
        p.imagePath = absolute;
    } else if (missingImage) {
        *missingImage = !absolute.isEmpty() ? absolute : fromRelative;
    }

    const QJsonObject t = o.value(QStringLiteral("transform")).toObject();
    p.transform.widthMm = t.value(QStringLiteral("widthMm")).toDouble(0.0);
    p.transform.heightMm = t.value(QStringLiteral("heightMm")).toDouble(0.0);
    if (p.transform.widthMm <= 0.0 || p.transform.heightMm <= 0.0) {
        return fail(tr("Im Projekt fehlt die Größe des Bildes."));
    }
    p.transform.widthMm = std::min(p.transform.widthMm, 100000.0);
    p.transform.heightMm = std::min(p.transform.heightMm, 100000.0);
    p.transform.rotationDeg = std::clamp(t.value(QStringLiteral("rotationDeg")).toDouble(0.0), -3600.0, 3600.0);
    p.transform.flipHorizontal = t.value(QStringLiteral("flipHorizontal")).toBool(false);
    p.transform.flipVertical = t.value(QStringLiteral("flipVertical")).toBool(false);
    p.transform.offsetXMm = std::clamp(t.value(QStringLiteral("offsetXMm")).toDouble(0.0), -100000.0, 100000.0);
    p.transform.offsetYMm = std::clamp(t.value(QStringLiteral("offsetYMm")).toDouble(0.0), -100000.0, 100000.0);

    const QJsonObject s = o.value(QStringLiteral("imageSettings")).toObject();
    const ImageSettings d;
    p.settings.method = methodFromName(s.value(QStringLiteral("method")).toString());
    p.settings.autoThreshold = s.value(QStringLiteral("autoThreshold")).toBool(d.autoThreshold);
    p.settings.threshold = std::clamp(s.value(QStringLiteral("threshold")).toInt(d.threshold), 0, 255);
    p.settings.brightness = std::clamp(s.value(QStringLiteral("brightness")).toInt(d.brightness), -100, 100);
    p.settings.contrast = std::clamp(s.value(QStringLiteral("contrast")).toInt(d.contrast), -100, 100);
    p.settings.gamma = std::clamp(s.value(QStringLiteral("gamma")).toDouble(d.gamma), 0.2, 3.0);
    p.settings.sharpen = std::clamp(s.value(QStringLiteral("sharpen")).toInt(d.sharpen), 0, 100);
    p.settings.invert = s.value(QStringLiteral("invert")).toBool(d.invert);
    p.settings.ditherCell = std::clamp(s.value(QStringLiteral("ditherCell")).toInt(d.ditherCell), 1, 8);
    p.settings.pixelSizeMm = std::clamp(s.value(QStringLiteral("pixelSizeMm")).toDouble(d.pixelSizeMm), 0.005, 10.0);
    p.settings.grayPower.minPercent = std::clamp(s.value(QStringLiteral("grayMinPercent")).toInt(d.grayPower.minPercent), 0, 100);
    p.settings.grayPower.maxPercent = std::clamp(s.value(QStringLiteral("grayMaxPercent")).toInt(d.grayPower.maxPercent), 1, 100);
    p.settings.grayPower.levels = std::clamp(s.value(QStringLiteral("grayLevels")).toInt(d.grayPower.levels), 8, 64);
    p.settings.svg.fullStrokes = s.value(QStringLiteral("svgFullStrokes")).toBool(d.svg.fullStrokes);
    p.settings.svg.cutStrokes = s.value(QStringLiteral("svgCutStrokes")).toBool(d.svg.cutStrokes);
    p.settings.svg.cutFills = s.value(QStringLiteral("svgCutFills")).toBool(d.svg.cutFills);
    p.settings.vectorize.turdSize = std::clamp(s.value(QStringLiteral("vectorTurdSize")).toInt(d.vectorize.turdSize), 0, 100);
    p.settings.vectorize.alphaMax = std::clamp(s.value(QStringLiteral("vectorAlphaMax")).toDouble(d.vectorize.alphaMax), 0.0, 1.34);
    p.settings.vectorize.optTolerance = std::clamp(s.value(QStringLiteral("vectorOptTolerance")).toDouble(d.vectorize.optTolerance), 0.0, 5.0);
    const QJsonArray crop = s.value(QStringLiteral("crop")).toArray();
    if (crop.size() == 4) {
        p.settings.crop = QRect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt());
    }

    const QJsonObject op = o.value(QStringLiteral("operation")).toObject();
    const Operation od;
    const int mode = op.value(QStringLiteral("mode")).toInt(0);
    p.operation.mode = (mode >= 0 && mode <= 2) ? static_cast<EngraveMode>(mode) : EngraveMode::Line;
    p.operation.angleDeg = std::clamp(op.value(QStringLiteral("angleDeg")).toDouble(od.angleDeg), 0.0, 180.0);
    p.operation.speedMmPerMin = std::clamp(op.value(QStringLiteral("speedMmPerMin")).toDouble(od.speedMmPerMin), 1.0, 100000.0);
    p.operation.powerPercent = std::clamp(op.value(QStringLiteral("powerPercent")).toDouble(od.powerPercent), 0.0, 100.0);
    p.operation.passes = std::clamp(op.value(QStringLiteral("passes")).toInt(od.passes), 1, 99);
    p.operation.overscanMm = std::clamp(op.value(QStringLiteral("overscanMm")).toDouble(od.overscanMm), 0.0, 100.0);
    p.operation.bidirectional = op.value(QStringLiteral("bidirectional")).toBool(od.bidirectional);
    p.operation.toleranceMm = std::clamp(op.value(QStringLiteral("toleranceMm")).toDouble(od.toleranceMm), 0.001, 1.0);
    p.operation.otherValues.speedMmPerMin = std::clamp(op.value(QStringLiteral("otherSpeedMmPerMin")).toDouble(od.otherValues.speedMmPerMin), 1.0, 100000.0);
    p.operation.otherValues.powerPercent = std::clamp(op.value(QStringLiteral("otherPowerPercent")).toDouble(od.otherValues.powerPercent), 0.0, 100.0);
    p.operation.otherValues.passes = std::clamp(op.value(QStringLiteral("otherPasses")).toInt(od.otherValues.passes), 1, 99);

    const int origin = o.value(QStringLiteral("jobOrigin")).toInt(0);
    p.origin = (origin >= 0 && origin <= 8) ? normalizedJobOrigin(static_cast<JobOrigin>(origin)) : JobOrigin::TopLeft;
    p.materialName = o.value(QStringLiteral("materialName")).toString();
    return p;
}

bool saveProjectFile(const QString& path, const ProjectData& project, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(projectToJson(project, path)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

std::optional<ProjectData> loadProjectFile(const QString& path, QString* error, QString* missingImage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = tr("Die Datei ist kein gültiges Projekt.");
        }
        return std::nullopt;
    }
    return projectFromJson(doc.object(), path, error, missingImage);
}

}  // namespace ld::core
