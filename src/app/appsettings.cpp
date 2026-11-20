#include "appsettings.h"

#include "core/ntfy.h"
#include "core/webauth.h"

#include <QCoreApplication>
#include <algorithm>
#include <cstdlib>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>

namespace ld::app {

namespace {
// Zahlen ohne Tausenderpunkt ("1200" statt "1.200"), aber mit Dezimalkomma der Sprache.
QString number(double value, int decimals)
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(value, 'f', decimals);
}

const QString kLength = QStringLiteral("units/length");
const QString kSpeed = QStringLiteral("units/speed");
const QString kResolution = QStringLiteral("units/resolution");
const QString kDesign = QStringLiteral("design/colorScheme");   // "system", "light", "dark" (wie bisher)
const QString kGrid = QStringLiteral("view/grid");
const QString kOrigin = QStringLiteral("job/defaultOrigin");

QColor readColor(const QSettings& s, const QString& key, const QColor& fallback)
{
    const QColor c(s.value(key).toString());
    return c.isValid() ? c : fallback;
}
}  // namespace

int AppSettings::weakLightMaxPercent() const
{
    return qBound(1, QSettings().value(QStringLiteral("laser/weakMaxPercent"), 5).toInt(), 20);
}

void AppSettings::setWeakLightMaxPercent(int percent)
{
    percent = qBound(1, percent, 20);
    if (percent == weakLightMaxPercent()) return;
    QSettings().setValue(QStringLiteral("laser/weakMaxPercent"), percent);
    emit laserChanged();
}

int AppSettings::weakLightAutoOffSeconds() const
{
    return qBound(0, QSettings().value(QStringLiteral("laser/weakAutoOff"), 30).toInt(), 3600);
}

void AppSettings::setWeakLightAutoOffSeconds(int seconds)
{
    seconds = qBound(0, seconds, 3600);
    if (seconds == weakLightAutoOffSeconds()) return;
    QSettings().setValue(QStringLiteral("laser/weakAutoOff"), seconds);
    emit laserChanged();
}

bool AppSettings::autoConnect() const
{
    return QSettings().value(QStringLiteral("connection/autoConnect"), true).toBool();
}

void AppSettings::setAutoConnect(bool on)
{
    QSettings().setValue(QStringLiteral("connection/autoConnect"), on);
}

bool AppSettings::webEnabled() const
{
    return QSettings().value(QStringLiteral("web/enabled"), false).toBool();
}

void AppSettings::setWebEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("web/enabled"), on);
}

int AppSettings::webPort() const
{
    return qBound(1024, QSettings().value(QStringLiteral("web/port"), 8765).toInt(), 65535);
}

void AppSettings::setWebPort(int port)
{
    QSettings().setValue(QStringLiteral("web/port"), qBound(1024, port, 65535));
}

ld::core::PasswordRecord AppSettings::webPasswordRecord() const
{
    return ld::core::PasswordRecord::fromString(QSettings().value(QStringLiteral("web/password")).toString());
}

void AppSettings::setWebPassword(const QString& password)
{
    QSettings().setValue(QStringLiteral("web/password"), ld::core::makePasswordRecord(password).toString());
}

int AppSettings::webPreviewSeconds() const
{
    const int saved = QSettings().value(QStringLiteral("web/previewSeconds"), 3).toInt();
    int best = 3, bestDiff = 1 << 30;   // nur erlaubte Werte; ein anderer wird auf den nächsten gerundet
    for (int choice : webPreviewChoices()) {
        const int diff = std::abs(choice - saved);
        if (diff < bestDiff) { bestDiff = diff; best = choice; }
    }
    return best;
}

void AppSettings::setWebPreviewSeconds(int seconds)
{
    QSettings().setValue(QStringLiteral("web/previewSeconds"), seconds);
}

bool AppSettings::webControlAllowed() const
{
    return QSettings().value(QStringLiteral("web/control"), false).toBool();
}

void AppSettings::setWebControlAllowed(bool on)
{
    QSettings().setValue(QStringLiteral("web/control"), on);
}

bool AppSettings::ntfyEnabled() const
{
    return QSettings().value(QStringLiteral("notify/ntfyEnabled"), false).toBool();
}

void AppSettings::setNtfyEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("notify/ntfyEnabled"), on);
}

QString AppSettings::ntfyServer() const
{
    return QSettings().value(QStringLiteral("notify/ntfyServer"), ld::core::defaultNtfyServer()).toString();
}

void AppSettings::setNtfyServer(const QString& server)
{
    QSettings().setValue(QStringLiteral("notify/ntfyServer"), server);
}

QString AppSettings::ntfyTopic() const
{
    return QSettings().value(QStringLiteral("notify/ntfyTopic")).toString();
}

void AppSettings::setNtfyTopic(const QString& topic)
{
    QSettings().setValue(QStringLiteral("notify/ntfyTopic"), topic);
}

bool AppSettings::ntfyNotifyFinished() const
{
    return QSettings().value(QStringLiteral("notify/ntfyFinished"), true).toBool();
}

void AppSettings::setNtfyNotifyFinished(bool on)
{
    QSettings().setValue(QStringLiteral("notify/ntfyFinished"), on);
}

bool AppSettings::showStartWarning() const
{
    return QSettings().value(QStringLiteral("start/showWarning"), true).toBool();
}

void AppSettings::setShowStartWarning(bool show)
{
    QSettings().setValue(QStringLiteral("start/showWarning"), show);
}

AppSettings& AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    const QSettings s;
    lengthUnit_ = s.value(kLength).toString() == QStringLiteral("in") ? LengthUnit::Inch : LengthUnit::Millimeter;
    speedUnit_ = s.value(kSpeed).toString() == QStringLiteral("mm/s") ? SpeedUnit::MmPerSec : SpeedUnit::MmPerMin;
    const QString resolution = s.value(kResolution, QStringLiteral("lines")).toString();
    resolutionUnit_ = resolution == QStringLiteral("dpi") ? ResolutionUnit::Dpi
                      : resolution == QStringLiteral("mm") ? ResolutionUnit::PixelSize : ResolutionUnit::LinesPerMm;
    const QString design = s.value(kDesign, QStringLiteral("system")).toString();
    design_ = design == QStringLiteral("dark") ? DesignChoice::Dark
              : design == QStringLiteral("light") ? DesignChoice::Light : DesignChoice::System;

    const DisplayColors d;
    colors_.progress = readColor(s, QStringLiteral("colors/progress"), d.progress);
    colors_.burn = readColor(s, QStringLiteral("colors/burn"), d.burn);
    colors_.travel = readColor(s, QStringLiteral("colors/travel"), d.travel);
    colors_.rapid = readColor(s, QStringLiteral("colors/rapid"), d.rapid);
    colors_.imageBurn = readColor(s, QStringLiteral("colors/imageBurn"), d.imageBurn);
    colors_.imagePaper = readColor(s, QStringLiteral("colors/imagePaper"), d.imagePaper);
    colors_.grid = readColor(s, QStringLiteral("colors/grid"), d.grid);
    colors_.gridStrength = qBound(0, s.value(QStringLiteral("colors/gridStrength"), d.gridStrength).toInt(), 100);
    colors_.gridVisible = s.value(kGrid, d.gridVisible).toBool();
    colors_.paperOpacity = qBound(0, s.value(QStringLiteral("colors/paperOpacity"), d.paperOpacity).toInt(), 100);

    autosaveMinutes_ = qBound(0, s.value(QStringLiteral("files/autosaveMinutes"), 2).toInt(), 60);
    recent_ = s.value(QStringLiteral("files/recent")).toStringList();

    const int origin = s.value(kOrigin, 0).toInt();
    defaultOrigin_ = (origin >= 0 && origin <= 8) ? ld::core::normalizedJobOrigin(static_cast<ld::core::JobOrigin>(origin))
                                                  : ld::core::JobOrigin::TopLeft;
}

void AppSettings::setLastOperation(const ld::core::Operation& op)
{
    QSettings s;
    s.beginGroup(QStringLiteral("lastOperation"));
    s.setValue(QStringLiteral("mode"), static_cast<int>(op.mode));
    s.setValue(QStringLiteral("angle"), op.angleDeg);
    s.setValue(QStringLiteral("speed"), op.speedMmPerMin);
    s.setValue(QStringLiteral("power"), op.powerPercent);
    s.setValue(QStringLiteral("passes"), op.passes);
    s.setValue(QStringLiteral("overscan"), op.overscanMm);
    s.setValue(QStringLiteral("bidirectional"), op.bidirectional);
    s.setValue(QStringLiteral("tolerance"), op.toleranceMm);
    s.setValue(QStringLiteral("otherSpeed"), op.otherValues.speedMmPerMin);
    s.setValue(QStringLiteral("otherPower"), op.otherValues.powerPercent);
    s.setValue(QStringLiteral("otherPasses"), op.otherValues.passes);
    s.endGroup();
}

std::optional<ld::core::Operation> AppSettings::lastOperation() const
{
    QSettings s;
    if (!s.contains(QStringLiteral("lastOperation/mode"))) return std::nullopt;
    s.beginGroup(QStringLiteral("lastOperation"));
    ld::core::Operation op;   // Vorgaben für alles, was fehlt
    const int mode = s.value(QStringLiteral("mode")).toInt();
    op.mode = mode == static_cast<int>(ld::core::EngraveMode::Full)      ? ld::core::EngraveMode::Full
              : mode == static_cast<int>(ld::core::EngraveMode::Cutting) ? ld::core::EngraveMode::Cutting
                                                                          : ld::core::EngraveMode::Line;
    op.angleDeg = qBound(0.0, s.value(QStringLiteral("angle"), op.angleDeg).toDouble(), 180.0);
    op.speedMmPerMin = qMax(1.0, s.value(QStringLiteral("speed"), op.speedMmPerMin).toDouble());
    op.powerPercent = qBound(0.0, s.value(QStringLiteral("power"), op.powerPercent).toDouble(), 100.0);
    op.passes = qBound(1, s.value(QStringLiteral("passes"), op.passes).toInt(), 99);
    op.overscanMm = qMax(0.0, s.value(QStringLiteral("overscan"), op.overscanMm).toDouble());
    op.bidirectional = s.value(QStringLiteral("bidirectional"), op.bidirectional).toBool();
    op.toleranceMm = qMax(0.001, s.value(QStringLiteral("tolerance"), op.toleranceMm).toDouble());
    op.otherValues.speedMmPerMin = qMax(1.0, s.value(QStringLiteral("otherSpeed"), op.otherValues.speedMmPerMin).toDouble());
    op.otherValues.powerPercent = qBound(0.0, s.value(QStringLiteral("otherPower"), op.otherValues.powerPercent).toDouble(), 100.0);
    op.otherValues.passes = qBound(1, s.value(QStringLiteral("otherPasses"), op.otherValues.passes).toInt(), 99);
    s.endGroup();
    return op;
}

void AppSettings::setLastFrame(double speedMmPerMin, double powerPercent)
{
    QSettings s;
    s.setValue(QStringLiteral("lastFrame/speed"), speedMmPerMin);
    s.setValue(QStringLiteral("lastFrame/power"), powerPercent);
}

std::optional<std::pair<double, double>> AppSettings::lastFrame() const
{
    const QSettings s;
    if (!s.contains(QStringLiteral("lastFrame/speed"))) return std::nullopt;
    return std::make_pair(qMax(1.0, s.value(QStringLiteral("lastFrame/speed")).toDouble()),
                          qBound(0.0, s.value(QStringLiteral("lastFrame/power")).toDouble(), 100.0));
}

void AppSettings::setLengthUnit(LengthUnit unit)
{
    if (unit == lengthUnit_) return;
    lengthUnit_ = unit;
    QSettings().setValue(kLength, unit == LengthUnit::Inch ? QStringLiteral("in") : QStringLiteral("mm"));
    emit unitsChanged();
}

void AppSettings::setSpeedUnit(SpeedUnit unit)
{
    if (unit == speedUnit_) return;
    speedUnit_ = unit;
    QSettings().setValue(kSpeed, unit == SpeedUnit::MmPerSec ? QStringLiteral("mm/s") : QStringLiteral("mm/min"));
    emit unitsChanged();
}

void AppSettings::setResolutionUnit(ResolutionUnit unit)
{
    if (unit == resolutionUnit_) return;
    resolutionUnit_ = unit;
    QSettings().setValue(kResolution, unit == ResolutionUnit::Dpi ? QStringLiteral("dpi")
                                      : unit == ResolutionUnit::PixelSize ? QStringLiteral("mm") : QStringLiteral("lines"));
    emit unitsChanged();
}

void AppSettings::setDesign(DesignChoice design)
{
    if (design == design_) return;
    design_ = design;
    QSettings().setValue(kDesign, design == DesignChoice::Dark ? QStringLiteral("dark")
                                  : design == DesignChoice::Light ? QStringLiteral("light") : QStringLiteral("system"));
    emit designChanged();
}

namespace {
QString folderKey(FolderKind kind, bool isDefault)
{
    const char* name = "Image";
    switch (kind) {
    case FolderKind::Images: name = "Image"; break;
    case FolderKind::Projects: name = "Project"; break;
    case FolderKind::Gcode: name = "Gcode"; break;
    case FolderKind::Nc: name = "Nc"; break;
    }
    return QStringLiteral("files/%1%2Dir").arg(isDefault ? QStringLiteral("default") : QStringLiteral("last"), QString::fromLatin1(name));
}
}  // namespace

QString AppSettings::defaultFolder(FolderKind kind) const
{
    return QSettings().value(folderKey(kind, true)).toString();
}

void AppSettings::setDefaultFolder(FolderKind kind, const QString& folder)
{
    QSettings().setValue(folderKey(kind, true), folder);
}

QString AppSettings::lastFolder(FolderKind kind) const
{
    QSettings s;
    QString folder = s.value(folderKey(kind, false)).toString();
    // NC-Dateien liegen oft dort, wo zuletzt G-Code gespeichert wurde.
    if (folder.isEmpty() && kind == FolderKind::Nc) folder = s.value(folderKey(FolderKind::Gcode, false)).toString();
    return folder;
}

void AppSettings::setLastFolder(FolderKind kind, const QString& pathOrFolder)
{
    const QFileInfo info(pathOrFolder);
    QSettings().setValue(folderKey(kind, false), info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

QString AppSettings::startFolder(FolderKind kind) const
{
    const QString standard = defaultFolder(kind);
    if (!standard.isEmpty() && QDir(standard).exists()) return standard;
    return lastFolder(kind);
}

QList<double> AppSettings::jogSteps() const
{
    QList<double> steps;
    for (const QString& text : QSettings().value(QStringLiteral("jog/steps")).toStringList()) {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok && value >= 0.01 && value <= 1000.0) steps.append(value);
    }
    if (steps.size() != 4) steps = {0.1, 1.0, 10.0, 100.0};
    std::sort(steps.begin(), steps.end());
    return steps;
}

int AppSettings::jogDefaultIndex() const
{
    return qBound(0, QSettings().value(QStringLiteral("jog/default"), 2).toInt(), 3);
}

void AppSettings::setJog(const QList<double>& steps, int defaultIndex)
{
    if (steps.size() != 4) return;
    QList<double> sorted = steps;
    const double chosen = steps.value(qBound(0, defaultIndex, 3));
    std::sort(sorted.begin(), sorted.end());
    QStringList text;
    for (const double value : sorted) text << QString::number(value, 'g', 6);
    QSettings s;
    s.setValue(QStringLiteral("jog/steps"), text);
    s.setValue(QStringLiteral("jog/default"), static_cast<int>(sorted.indexOf(chosen)));   // der gewählte Wert bleibt nach dem Sortieren der vorgewählte
    emit jogChanged();
}

bool AppSettings::consoleShowStatus() const
{
    return QSettings().value(QStringLiteral("console/showStatus"), false).toBool();
}

int AppSettings::consoleMaxLines() const
{
    return qBound(500, QSettings().value(QStringLiteral("console/maxLines"), 3000).toInt(), 20000);
}

void AppSettings::setConsole(bool showStatus, int maxLines)
{
    QSettings s;
    s.setValue(QStringLiteral("console/showStatus"), showStatus);
    s.setValue(QStringLiteral("console/maxLines"), qBound(500, maxLines, 20000));
    emit consoleChanged();
}

QString AppSettings::language() const
{
    const QString value = QSettings().value(QStringLiteral("general/language"), QStringLiteral("system")).toString();
    return value == QLatin1String("de") || value == QLatin1String("en") ? value : QStringLiteral("system");
}

void AppSettings::setLanguage(const QString& language)
{
    QSettings().setValue(QStringLiteral("general/language"), language);
}

void AppSettings::setColors(const DisplayColors& colors)
{
    if (colors == colors_) return;
    colors_ = colors;
    QSettings s;
    s.setValue(QStringLiteral("colors/progress"), colors.progress.name());
    s.setValue(QStringLiteral("colors/burn"), colors.burn.name());
    s.setValue(QStringLiteral("colors/travel"), colors.travel.name());
    s.setValue(QStringLiteral("colors/rapid"), colors.rapid.name());
    s.setValue(QStringLiteral("colors/imageBurn"), colors.imageBurn.name());
    s.setValue(QStringLiteral("colors/imagePaper"), colors.imagePaper.name());
    s.setValue(QStringLiteral("colors/grid"), colors.grid.name());
    s.setValue(QStringLiteral("colors/gridStrength"), colors.gridStrength);
    s.setValue(kGrid, colors.gridVisible);
    s.setValue(QStringLiteral("colors/paperOpacity"), colors.paperOpacity);
    emit colorsChanged();
}

void AppSettings::setGridVisible(bool visible)
{
    if (visible == colors_.gridVisible) return;
    DisplayColors c = colors_;
    c.gridVisible = visible;
    setColors(c);
}

void AppSettings::setDefaultJobOrigin(ld::core::JobOrigin origin)
{
    if (origin == defaultOrigin_) return;
    defaultOrigin_ = origin;
    QSettings().setValue(kOrigin, static_cast<int>(origin));
    emit defaultJobOriginChanged();
}

void AppSettings::setAutosaveMinutes(int minutes)
{
    minutes = qBound(0, minutes, 60);
    if (minutes == autosaveMinutes_) return;
    autosaveMinutes_ = minutes;
    QSettings().setValue(QStringLiteral("files/autosaveMinutes"), minutes);
    emit autosaveChanged();
}

void AppSettings::addRecentProject(const QString& path)
{
    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > 10) {
        recent_.removeLast();
    }
    QSettings().setValue(QStringLiteral("files/recent"), recent_);
    emit recentChanged();
}

void AppSettings::removeRecentProject(const QString& path)
{
    if (recent_.removeAll(path) > 0) {
        QSettings().setValue(QStringLiteral("files/recent"), recent_);
        emit recentChanged();
    }
}

void AppSettings::clearRecentProjects()
{
    recent_.clear();
    QSettings().setValue(QStringLiteral("files/recent"), recent_);
    emit recentChanged();
}

// ---------------------------------------------------------------------------------- Einheiten
double mmPerLengthUnit()
{
    return AppSettings::instance().lengthUnit() == LengthUnit::Inch ? 25.4 : 1.0;
}

double mmToDisplay(double mm) { return mm / mmPerLengthUnit(); }
double displayToMm(double value) { return value * mmPerLengthUnit(); }

QString lengthSuffix()
{
    return AppSettings::instance().lengthUnit() == LengthUnit::Inch ? QStringLiteral(" in") : QStringLiteral(" mm");
}

QString formatLength(double mm, int decimalsMm)
{
    const bool inch = AppSettings::instance().lengthUnit() == LengthUnit::Inch;
    const int decimals = inch ? decimalsMm + 2 : decimalsMm;
    return number(mmToDisplay(mm), decimals) + lengthSuffix();
}

double speedToDisplay(double mmPerMin)
{
    return AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec ? mmPerMin / 60.0 : mmPerMin;
}

double displayToSpeed(double value)
{
    return AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec ? value * 60.0 : value;
}

QString speedSuffix()
{
    return AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec ? QStringLiteral(" mm/s") : QStringLiteral(" mm/min");
}

QString formatSpeed(double mmPerMin)
{
    const bool perSecond = AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec;
    return number(speedToDisplay(mmPerMin), perSecond ? 1 : 0) + speedSuffix();
}

QString resolutionLabel()
{
    switch (AppSettings::instance().resolutionUnit()) {
    case ResolutionUnit::Dpi: return QCoreApplication::translate("ld::app", "Auflösung");
    case ResolutionUnit::PixelSize: return QCoreApplication::translate("ld::app", "Pixelgröße");
    case ResolutionUnit::LinesPerMm: break;
    }
    return QCoreApplication::translate("ld::app", "Linien/mm");
}

QString formatResolution(double mmPerPixel)
{
    if (mmPerPixel <= 0.0) return QString();
    const double lines = 1.0 / mmPerPixel;   // Linien pro mm
    const double dpi = lines * 25.4;
    switch (AppSettings::instance().resolutionUnit()) {
    case ResolutionUnit::Dpi:
        return QStringLiteral("%1 dpi").arg(number(dpi, 0));
    case ResolutionUnit::PixelSize:
        return formatLength(mmPerPixel, 4);
    case ResolutionUnit::LinesPerMm: break;
    }
    return QCoreApplication::translate("ld::app", "%1 Linien/mm (%2 dpi)").arg(number(lines, 1), number(dpi, 0));
}

}  // namespace ld::app
