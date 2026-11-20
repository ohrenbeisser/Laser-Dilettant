// Programmeinstellungen (siehe docs/planung.md 3.12): Design, Einheiten, Farben, Raster, Standard-
// Job-Ursprung. Eine zentrale Stelle, die in QSettings speichert und Änderungen meldet; alle Felder
// und Anzeigen holen sich ihre Werte von hier.
#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

#include "core/webauth.h"
#include "core/engraving.h"

namespace ld::app {

enum class LengthUnit { Millimeter, Inch };
enum class SpeedUnit { MmPerMin, MmPerSec };
// Wie die Auflösung (Pixelgröße) angezeigt wird: Linien pro mm (Vorgabe, mit dpi in Klammern), dpi oder Pixelgröße in mm.
enum class ResolutionUnit { LinesPerMm, Dpi, PixelSize };
enum class DesignChoice { System, Light, Dark };
// Ordner für die Dateidialoge: Bilder, Projekte, G-Code (Speichern) und NC-Dateien (Laden).
enum class FolderKind { Images, Projects, Gcode, Nc };

// Farben der Anzeige und das Raster.
struct DisplayColors {
    QColor progress = QColor(0x2e, 0x9e, 0x4f);   // gelaserter Teil bei der Simulation
    QColor burn = QColor(0xa0, 0x18, 0x18);       // Laserweg mit Leistung
    QColor travel = QColor(0x9a, 0xa0, 0xa6);     // Leerfahrt
    QColor rapid = QColor(0xe0, 0xa8, 0x00);      // Eilgang (gelb)
    QColor imageBurn = QColor(0, 0, 0);           // "gebrannt" in der Bildanzeige
    QColor imagePaper = QColor(255, 255, 255);    // "Papier" in der Bildanzeige
    QColor grid = QColor(0x8a, 0x8a, 0x90);       // Farbe der Rasterlinien
    int gridStrength = 40;                        // 0..100: wie deutlich das Raster ist
    bool gridVisible = true;
    int paperOpacity = 50;                        // 0..100 %: Deckkraft des weißen Grundes der Arbeitsfläche (Raster scheint durch)

    friend bool operator==(const DisplayColors&, const DisplayColors&) = default;
};

class AppSettings : public QObject {
    Q_OBJECT

public:
    static AppSettings& instance();

    LengthUnit lengthUnit() const { return lengthUnit_; }
    SpeedUnit speedUnit() const { return speedUnit_; }
    ResolutionUnit resolutionUnit() const { return resolutionUnit_; }
    DesignChoice design() const { return design_; }
    const DisplayColors& colors() const { return colors_; }
    ld::core::JobOrigin defaultJobOrigin() const { return defaultOrigin_; }
    // Wiederherstellung: alle so viele Minuten wird gesichert (0 = aus). Vorgabe 2.
    int autosaveMinutes() const { return autosaveMinutes_; }
    // Warnhinweis vor dem Start eines Auftrags am Laser (Schutzbrille, Material fixiert ...).
    // Beim Programmstart automatisch mit dem zuletzt benutzten Port verbinden (Vorgabe: an).
    // Schwachlicht (siehe planung.md 3.16): Obergrenze der Leistung in % (1..20, Vorgabe 5) und Auto-Aus in Sekunden (0 = nie, Vorgabe 30).
    int weakLightMaxPercent() const;
    void setWeakLightMaxPercent(int percent);
    int weakLightAutoOffSeconds() const;
    void setWeakLightAutoOffSeconds(int seconds);
    bool autoConnect() const;
    void setAutoConnect(bool on);
    // Benachrichtigung über ntfy (planung.md 3.40): aus (Vorgabe), Server, Thema (wie ein Passwort) und ob auch "Auftrag fertig" kommt.
    bool ntfyEnabled() const;
    void setNtfyEnabled(bool on);
    QString ntfyServer() const;
    void setNtfyServer(const QString& server);
    QString ntfyTopic() const;
    void setNtfyTopic(const QString& topic);
    // Fernansicht (planung.md 3.41): aus (Vorgabe), Port (8765), Passwort (nur als Hash), Fernsteuerung erlaubt (aus).
    bool webEnabled() const;
    void setWebEnabled(bool on);
    int webPort() const;
    void setWebPort(int port);
    ld::core::PasswordRecord webPasswordRecord() const;
    void setWebPassword(const QString& password);   // speichert nur den Hash
    // Wie oft die Fernansicht das Bild des Gebrannten abholt (Sekunden; erlaubt sind 1, 2, 3, 4, 5, 10, 20, 30, 60; Vorgabe 3).
    static QList<int> webPreviewChoices() { return {1, 2, 3, 4, 5, 10, 20, 30, 60}; }
    int webPreviewSeconds() const;
    void setWebPreviewSeconds(int seconds);
    bool webControlAllowed() const;
    void setWebControlAllowed(bool on);
    bool ntfyNotifyFinished() const;
    void setNtfyNotifyFinished(bool on);
    bool showStartWarning() const;
    void setShowStartWarning(bool show);
    // Standardordner der Dateidialoge (leer = kein Standard: dann gilt der zuletzt benutzte Ordner).
    QString defaultFolder(FolderKind kind) const;
    void setDefaultFolder(FolderKind kind, const QString& folder);
    // Der zuletzt benutzte Ordner (merkt sich jeder Dialog nach dem Auswählen einer Datei).
    QString lastFolder(FolderKind kind) const;
    void setLastFolder(FolderKind kind, const QString& pathOrFolder);
    // Womit ein Dateidialog startet: der Standardordner, falls gesetzt und vorhanden, sonst der zuletzt benutzte.
    QString startFolder(FolderKind kind) const;
    // Jog-Schritte (mm): vier Werte aufsteigend, und welcher beim Start vorgewählt ist (0..3). Vorgabe 0,1 / 1 / 10 / 100, vorgewählt 10.
    QList<double> jogSteps() const;
    int jogDefaultIndex() const;
    void setJog(const QList<double>& steps, int defaultIndex);
    // Konsole: Statusabfragen von GRBL anzeigen (Vorgabe aus) und wie viele Zeilen der Verlauf behält (Vorgabe 3000).
    bool consoleShowStatus() const;
    int consoleMaxLines() const;
    void setConsole(bool showStatus, int maxLines);
    // Sprache der Oberfläche: "system" (Vorgabe), "de" oder "en". Wirkt nach einem Neustart.
    QString language() const;
    void setLanguage(const QString& language);
    // Zuletzt geöffnete Projekte, neueste zuerst (höchstens 10).
    QStringList recentProjects() const { return recent_; }

    void setLengthUnit(LengthUnit unit);
    void setSpeedUnit(SpeedUnit unit);
    void setResolutionUnit(ResolutionUnit unit);
    void setDesign(DesignChoice design);
    void setColors(const DisplayColors& colors);
    void setGridVisible(bool visible);
    void setDefaultJobOrigin(ld::core::JobOrigin origin);
    void setAutosaveMinutes(int minutes);
    // Zuletzt benutzte Gravur-Einstellungen (Gravurart, Speed, Leistung, Durchgänge, Winkel ...) und Rahmenwerte.
    // Sie werden beim Beenden gemerkt und beim nächsten Start als Ausgangswerte gesetzt (ein Projekt überschreibt sie).
    void setLastOperation(const ld::core::Operation& operation);
    std::optional<ld::core::Operation> lastOperation() const;
    void setLastFrame(double speedMmPerMin, double powerPercent);
    std::optional<std::pair<double, double>> lastFrame() const;
    void addRecentProject(const QString& path);
    void removeRecentProject(const QString& path);
    void clearRecentProjects();

signals:
    void unitsChanged();
    void designChanged();
    void colorsChanged();    // Farben oder Raster
    void defaultJobOriginChanged();
    void autosaveChanged();
    void laserChanged();     // Schwachlicht-Einstellungen
    void jogChanged();       // Jog-Schritte
    void consoleChanged();   // Konsole
    void recentChanged();

private:
    AppSettings();

    LengthUnit lengthUnit_ = LengthUnit::Millimeter;
    SpeedUnit speedUnit_ = SpeedUnit::MmPerMin;
    ResolutionUnit resolutionUnit_ = ResolutionUnit::LinesPerMm;
    DesignChoice design_ = DesignChoice::System;
    DisplayColors colors_;
    ld::core::JobOrigin defaultOrigin_ = ld::core::JobOrigin::TopLeft;
    int autosaveMinutes_ = 2;
    QStringList recent_;
};

// ---- Umrechnung und Anzeige in der gewählten Einheit (der G-Code bleibt immer in mm) ----
double mmPerLengthUnit();                    // 1 oder 25,4
double mmToDisplay(double mm);
double displayToMm(double value);
QString lengthSuffix();                       // " mm" oder " in"
// Länge als Text in der gewählten Einheit, mit Einheit ("63,5 mm", "2,5 in").
QString formatLength(double mm, int decimalsMm = 1);

double speedToDisplay(double mmPerMin);       // mm/min oder mm/s
double displayToSpeed(double value);
QString speedSuffix();                        // " mm/min" oder " mm/s"
QString formatSpeed(double mmPerMin);

// Auflösung (Pixelgröße in mm) in der gewählten Einheit: Linien/mm (mit dpi in Klammern), dpi oder Pixelgröße.
QString resolutionLabel();                    // Beschriftung des Feldes: "Linien/mm", "Auflösung (dpi)" oder "Pixelgröße"
QString formatResolution(double mmPerPixel);

}  // namespace ld::app
