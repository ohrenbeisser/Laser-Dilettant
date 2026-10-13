#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// Typed access to the program settings (QSettings). Emits changed() when a
// value was modified through this class.
class AppSettings : public QObject
{
    Q_OBJECT
public:
    enum class Theme { System, Light, Dark };
    enum class AfterJob { Origin, StartPosition, Stay };

    explicit AppSettings(QObject* parent = nullptr);

    QSettings& raw() { return m_settings; }

    // "de", "en" or "" (system language)
    QString language() const;
    void setLanguage(const QString& lang);

    Theme theme() const;
    void setTheme(Theme t);

    bool confirmBeforeStart() const;
    void setConfirmBeforeStart(bool on);

    AfterJob afterJob() const;
    void setAfterJob(AfterJob a);

    bool frameWithLaser() const;
    void setFrameWithLaser(bool on);

    int pollIntervalMs() const;
    void setPollIntervalMs(int ms);

    int focusTimeoutS() const;
    void setFocusTimeoutS(int s);

    // Switch off laser mode ($32=0) while the focus laser is on.
    bool focusDisablesLaserMode() const;
    void setFocusDisablesLaserMode(bool on);

    bool commLogEnabled() const;
    void setCommLogEnabled(bool on);
    QString commLogDirectory() const;

    double rapidThresholdMm() const;
    void setRapidThresholdMm(double mm);

    QString avrdudePath() const;
    void setAvrdudePath(const QString& path);

    // Set while $32 was switched off by the focus laser and not restored yet.
    // Survives a crash, so the next connection can restore it.
    int pendingLaserModeRestore() const; // -1 = nothing pending
    void setPendingLaserModeRestore(int value);

    QString lastPort() const;
    void setLastPort(const QString& port);
    int lastBaud() const;
    void setLastBaud(int baud);
    QString lastProfileId() const;
    void setLastProfileId(const QString& id);

    static QString themeToString(Theme t);
    static Theme themeFromString(const QString& s);

signals:
    void changed();

private:
    void set(const QString& key, const QVariant& value);
    QSettings m_settings;
};
