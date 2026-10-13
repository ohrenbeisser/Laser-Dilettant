#include "app/AppSettings.h"

#include <QStandardPaths>

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
{
}

void AppSettings::set(const QString& key, const QVariant& value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    emit changed();
}

QString AppSettings::language() const { return m_settings.value(QStringLiteral("program/language")).toString(); }
void AppSettings::setLanguage(const QString& lang) { set(QStringLiteral("program/language"), lang); }

QString AppSettings::themeToString(Theme t)
{
    switch (t) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::Dark: return QStringLiteral("dark");
    case Theme::System: break;
    }
    return QStringLiteral("system");
}

AppSettings::Theme AppSettings::themeFromString(const QString& s)
{
    if (s == QLatin1String("light"))
        return Theme::Light;
    if (s == QLatin1String("dark"))
        return Theme::Dark;
    return Theme::System;
}

AppSettings::Theme AppSettings::theme() const
{
    return themeFromString(m_settings.value(QStringLiteral("program/theme")).toString());
}
void AppSettings::setTheme(Theme t) { set(QStringLiteral("program/theme"), themeToString(t)); }

bool AppSettings::confirmBeforeStart() const { return m_settings.value(QStringLiteral("program/confirmStart"), true).toBool(); }
void AppSettings::setConfirmBeforeStart(bool on) { set(QStringLiteral("program/confirmStart"), on); }

AppSettings::AfterJob AppSettings::afterJob() const
{
    const QString v = m_settings.value(QStringLiteral("program/afterJob"), QStringLiteral("origin")).toString();
    if (v == QLatin1String("start"))
        return AfterJob::StartPosition;
    if (v == QLatin1String("stay"))
        return AfterJob::Stay;
    return AfterJob::Origin;
}
void AppSettings::setAfterJob(AfterJob a)
{
    set(QStringLiteral("program/afterJob"),
        a == AfterJob::StartPosition ? QStringLiteral("start") : a == AfterJob::Stay ? QStringLiteral("stay") : QStringLiteral("origin"));
}

bool AppSettings::frameWithLaser() const { return m_settings.value(QStringLiteral("program/frameWithLaser"), false).toBool(); }
void AppSettings::setFrameWithLaser(bool on) { set(QStringLiteral("program/frameWithLaser"), on); }

int AppSettings::pollIntervalMs() const { return qBound(100, m_settings.value(QStringLiteral("device/pollIntervalMs"), 200).toInt(), 1000); }
void AppSettings::setPollIntervalMs(int ms) { set(QStringLiteral("device/pollIntervalMs"), qBound(100, ms, 1000)); }

int AppSettings::focusTimeoutS() const { return qBound(5, m_settings.value(QStringLiteral("device/focusTimeoutS"), 60).toInt(), 600); }
void AppSettings::setFocusTimeoutS(int s) { set(QStringLiteral("device/focusTimeoutS"), qBound(5, s, 600)); }

bool AppSettings::focusDisablesLaserMode() const { return m_settings.value(QStringLiteral("device/focusDisablesLaserMode"), false).toBool(); }
void AppSettings::setFocusDisablesLaserMode(bool on) { set(QStringLiteral("device/focusDisablesLaserMode"), on); }

bool AppSettings::commLogEnabled() const { return m_settings.value(QStringLiteral("device/commLog"), false).toBool(); }
void AppSettings::setCommLogEnabled(bool on) { set(QStringLiteral("device/commLog"), on); }
QString AppSettings::commLogDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
}

double AppSettings::rapidThresholdMm() const { return m_settings.value(QStringLiteral("generator/rapidThresholdMm"), 5.0).toDouble(); }
void AppSettings::setRapidThresholdMm(double mm) { set(QStringLiteral("generator/rapidThresholdMm"), mm); }

QString AppSettings::avrdudePath() const { return m_settings.value(QStringLiteral("firmware/avrdudePath")).toString(); }
void AppSettings::setAvrdudePath(const QString& path) { set(QStringLiteral("firmware/avrdudePath"), path); }

int AppSettings::pendingLaserModeRestore() const { return m_settings.value(QStringLiteral("device/pendingLaserModeRestore"), -1).toInt(); }
void AppSettings::setPendingLaserModeRestore(int value)
{
    m_settings.setValue(QStringLiteral("device/pendingLaserModeRestore"), value);
    m_settings.sync();
}

QString AppSettings::lastPort() const { return m_settings.value(QStringLiteral("connection/port")).toString(); }
void AppSettings::setLastPort(const QString& port) { m_settings.setValue(QStringLiteral("connection/port"), port); }
int AppSettings::lastBaud() const { return m_settings.value(QStringLiteral("connection/baud"), 0).toInt(); }
void AppSettings::setLastBaud(int baud) { m_settings.setValue(QStringLiteral("connection/baud"), baud); }
QString AppSettings::lastProfileId() const { return m_settings.value(QStringLiteral("connection/profile"), QStringLiteral("diy8000_4050")).toString(); }
void AppSettings::setLastProfileId(const QString& id) { m_settings.setValue(QStringLiteral("connection/profile"), id); }
