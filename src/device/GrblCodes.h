#pragma once

#include <QHash>
#include <QString>

// Human-readable texts for GRBL error codes, alarm codes and settings, read
// from the CSV files in resources/grbl (taken from gnea/grbl/doc/csv and
// translated).
class GrblCodes
{
public:
    struct SettingInfo
    {
        QString name;
        QString unit;
        QString description;
    };

    // language: "de" or "en"; anything else falls back to English.
    static void setLanguage(const QString& language);

    static QString errorText(int code);
    static QString alarmText(int code);
    // Short form, e.g. "Undefined feed rate".
    static QString errorShort(int code);
    static QString alarmShort(int code);
    static bool hasSetting(int number);
    static SettingInfo setting(int number);
    static QList<int> knownSettings();

    // Reads a CSV file as used by GRBL (all fields quoted, "" escapes a quote).
    static QList<QStringList> parseCsv(const QString& text);

private:
    static GrblCodes& instance();
    void load(const QString& language);

    struct Entry
    {
        QString shortText;
        QString longText;
    };
    QString m_language;
    QHash<int, Entry> m_errors;
    QHash<int, Entry> m_alarms;
    QHash<int, SettingInfo> m_settings;
};
