#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// The "$N=value" settings of a GRBL controller.
class GrblSettings
{
public:
    // Parses one "$N=value" line. Returns false for anything else.
    static bool parseLine(const QString& line, int* number, QString* value);

    bool parseAndStore(const QString& line);

    void set(int number, const QString& value) { m_values[number] = value; }
    bool has(int number) const { return m_values.contains(number); }
    QString raw(int number) const { return m_values.value(number); }
    double number(int n, double fallback = 0.0) const;
    bool isEmpty() const { return m_values.isEmpty(); }
    void clear() { m_values.clear(); }
    const QMap<int, QString>& values() const { return m_values; }

    // Text file with one "$N=value" per line; '#' or ';' start comments.
    QString toText(const QString& header = QString()) const;
    static GrblSettings fromText(const QString& text);

private:
    QMap<int, QString> m_values;
};
