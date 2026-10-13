#include "device/GrblCodes.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>

#include <algorithm>

GrblCodes& GrblCodes::instance()
{
    static GrblCodes codes;
    if (codes.m_language.isEmpty())
        codes.load(QLocale().language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en"));
    return codes;
}

void GrblCodes::setLanguage(const QString& language)
{
    instance().load(language);
}

QList<QStringList> GrblCodes::parseCsv(const QString& text)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;
    auto endField = [&] {
        row << field.trimmed();
        field.clear();
        fieldStarted = false;
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text[i + 1] == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == QLatin1Char(',')) {
            endField();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (fieldStarted || !row.isEmpty()) {
                endField();
                rows << row;
                row.clear();
            }
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !row.isEmpty()) {
        endField();
        rows << row;
    }
    return rows;
}

void GrblCodes::load(const QString& language)
{
    const QString suffix = language == QLatin1String("de") ? QStringLiteral("de_DE") : QStringLiteral("en_US");
    m_language = language;
    m_errors.clear();
    m_alarms.clear();
    m_settings.clear();

    auto read = [&](const QString& base) {
        QFile f(QStringLiteral(":/grbl/%1_%2.csv").arg(base, suffix));
        if (!f.open(QIODevice::ReadOnly))
            return QList<QStringList>();
        QList<QStringList> rows = parseCsv(QString::fromUtf8(f.readAll()));
        if (!rows.isEmpty())
            rows.removeFirst(); // header
        return rows;
    };

    for (const QStringList& r : read(QStringLiteral("error_codes"))) {
        bool ok = false;
        const int code = r.value(0).toInt(&ok);
        if (ok)
            m_errors.insert(code, { r.value(1), r.value(2) });
    }
    for (const QStringList& r : read(QStringLiteral("alarm_codes"))) {
        bool ok = false;
        const int code = r.value(0).toInt(&ok);
        if (ok)
            m_alarms.insert(code, { r.value(1), r.value(2) });
    }
    for (const QStringList& r : read(QStringLiteral("setting_codes"))) {
        bool ok = false;
        const int code = r.value(0).toInt(&ok);
        if (ok)
            m_settings.insert(code, { r.value(1), r.value(2), r.value(3) });
    }
}

QString GrblCodes::errorText(int code)
{
    const auto& e = instance().m_errors;
    if (!e.contains(code))
        return QCoreApplication::translate("GrblCodes", "Unknown error %1").arg(code);
    return e.value(code).longText;
}

QString GrblCodes::errorShort(int code)
{
    const auto& e = instance().m_errors;
    if (!e.contains(code))
        return QCoreApplication::translate("GrblCodes", "Unknown error %1").arg(code);
    return e.value(code).shortText;
}

QString GrblCodes::alarmText(int code)
{
    const auto& a = instance().m_alarms;
    if (!a.contains(code))
        return QCoreApplication::translate("GrblCodes", "Unknown alarm %1").arg(code);
    return a.value(code).longText;
}

QString GrblCodes::alarmShort(int code)
{
    const auto& a = instance().m_alarms;
    if (!a.contains(code))
        return QCoreApplication::translate("GrblCodes", "Unknown alarm %1").arg(code);
    return a.value(code).shortText;
}

bool GrblCodes::hasSetting(int number)
{
    return instance().m_settings.contains(number);
}

GrblCodes::SettingInfo GrblCodes::setting(int number)
{
    return instance().m_settings.value(number);
}

QList<int> GrblCodes::knownSettings()
{
    QList<int> keys = instance().m_settings.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}
