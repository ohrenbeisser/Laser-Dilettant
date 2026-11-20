#include "core/protocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>
#include <algorithm>

namespace ld::core {

namespace {

const char kTimeFormat[] = "yyyy-MM-dd HH:mm:ss";

// Kopfzeilen der beiden Dateien (die Reihenfolge der Spalten ist Teil des Formats).
const QStringList kJobHeader = {
    QStringLiteral("ID"), QStringLiteral("Start"), QStringLiteral("Zeitzone"), QStringLiteral("Ende"), QStringLiteral("Name"),
    QStringLiteral("Art"), QStringLiteral("Modus"), QStringLiteral("Speed (mm/min)"), QStringLiteral("Power (%)"),
    QStringLiteral("Durchgänge"), QStringLiteral("Breite (mm)"), QStringLiteral("Höhe (mm)"), QStringLiteral("Material"),
    QStringLiteral("Profil"), QStringLiteral("Ergebnis"), QStringLiteral("Laufzeit (s)"), QStringLiteral("Arbeitszeit (s)"),
    QStringLiteral("Geschätzt (s)"), QStringLiteral("Zeilen"), QStringLiteral("Hinweis")};
const QStringList kSystemHeader = {QStringLiteral("Zeit"), QStringLiteral("Zeitzone"), QStringLiteral("Stufe"),
                                   QStringLiteral("Kategorie"), QStringLiteral("Meldung"), QStringLiteral("Details")};

// Nur eine Zeile: Zeilenumbrüche und Tabulatoren in Texten werden zu Leerzeichen.
QString oneLine(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")), QStringLiteral(" "));
    return text.trimmed();
}

QString zoneText(const QDateTime& time)
{
    const int offset = time.offsetFromUtc();
    const int minutes = std::abs(offset) / 60;
    return QStringLiteral("%1%2:%3").arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString timeText(const QDateTime& time)
{
    return time.isValid() ? time.toString(QLatin1String(kTimeFormat)) : QString();
}

QDateTime timeFromText(const QString& text)
{
    return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, QLatin1String(kTimeFormat));
}

// Liest eine Datei als Zeilenliste (UTF-8, mit oder ohne BOM), die Kopfzeile fällt weg.
QStringList readDataLines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
    }
    if (!lines.isEmpty()) lines.removeFirst();   // Kopfzeile
    return lines;
}

// Hängt eine Zeile an (legt die Datei mit BOM und Kopfzeile an, wenn sie neu ist). Gibt die Stelle der neuen Zeile zurück.
qint64 appendLine(const QString& path, const QStringList& header, const QString& line)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::WriteOnly)) return -1;
    if (file.size() == 0) {
        file.write("\xEF\xBB\xBF");
        file.write((csvLine(header) + QLatin1Char('\n')).toUtf8());
    }
    const qint64 offset = file.size();
    file.write((line + QLatin1Char('\n')).toUtf8());
    file.flush();
    return offset;
}

QString monthName(const QDateTime& time)
{
    return time.toString(QStringLiteral("yyyy-MM"));
}

// Dateien "prefix-JJJJ-MM.csv" im Ordner, älteste zuerst, nur die Monate im Zeitraum.
QStringList listFiles(const QString& folder, const QString& prefix, const QDate& from, const QDate& to)
{
    QStringList result;
    const QRegularExpression pattern(QStringLiteral("^%1-(\\d{4})-(\\d{2})\\.csv$").arg(prefix));
    for (const QString& name : QDir(folder).entryList(QDir::Files, QDir::Name)) {
        const auto match = pattern.match(name);
        if (!match.hasMatch()) continue;
        const QDate firstDay(match.captured(1).toInt(), match.captured(2).toInt(), 1);
        if (!firstDay.isValid()) continue;
        if (to.isValid() && firstDay > to) continue;
        if (from.isValid() && firstDay.addMonths(1).addDays(-1) < from) continue;
        result << QDir(folder).filePath(name);
    }
    return result;
}

QStringList jobFieldsImpl(const JobRecord& r)
{
    return {r.id, timeText(r.start), r.start.isValid() ? zoneText(r.start) : QString(), timeText(r.end), oneLine(r.name),
            oneLine(r.kind), oneLine(r.mode), csvNumber(r.speedMmPerMin, 0), csvNumber(r.powerPercent, 0),
            QString::number(r.passes), csvNumber(r.widthMm, 1), csvNumber(r.heightMm, 1), oneLine(r.material),
            oneLine(r.profile), jobResultText(r.result), QString::number(r.runSeconds), QString::number(r.activeSeconds),
            QString::number(r.estimateSeconds), QString::number(r.lines), oneLine(r.note)};
}

bool jobFromFields(const QStringList& f, JobRecord* out)
{
    if (f.size() < kJobHeader.size() - 1 || f.value(0).isEmpty()) return false;   // der Hinweis darf fehlen
    JobRecord r;
    r.id = f[0];
    r.start = timeFromText(f[1]);
    r.end = timeFromText(f[3]);
    r.name = f[4];
    r.kind = f[5];
    r.mode = f[6];
    r.speedMmPerMin = parseCsvNumber(f[7]);
    r.powerPercent = parseCsvNumber(f[8]);
    r.passes = f[9].toInt();
    r.widthMm = parseCsvNumber(f[10]);
    r.heightMm = parseCsvNumber(f[11]);
    r.material = f[12];
    r.profile = f[13];
    r.result = jobResultFromText(f[14]);
    r.runSeconds = f[15].toInt();
    r.activeSeconds = f[16].toInt();
    r.estimateSeconds = f[17].toInt();
    r.lines = f[18].toInt();
    r.note = f.value(19);
    *out = r;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Texte
// ---------------------------------------------------------------------------------------------
QString jobResultText(JobResult result)
{
    switch (result) {
    case JobResult::Running: return QStringLiteral("läuft");
    case JobResult::Finished: return QStringLiteral("fertig");
    case JobResult::CancelledByUser: return QStringLiteral("abgebrochen");
    case JobResult::EmergencyStop: return QStringLiteral("NOT-AUS");
    case JobResult::Alarm: return QStringLiteral("Alarm");
    case JobResult::Error: return QStringLiteral("Fehler");
    case JobResult::ConnectionLost: return QStringLiteral("Verbindung verloren");
    case JobResult::GrblReset: return QStringLiteral("GRBL zurückgesetzt");
    case JobResult::Aborted: return QStringLiteral("abgebrochen (Sonstiges)");
    case JobResult::Interrupted: return QStringLiteral("unterbrochen");
    }
    return QStringLiteral("abgebrochen (Sonstiges)");
}

JobResult jobResultFromText(const QString& text)
{
    for (const JobResult r : {JobResult::Running, JobResult::Finished, JobResult::CancelledByUser, JobResult::EmergencyStop,
                              JobResult::Alarm, JobResult::Error, JobResult::ConnectionLost, JobResult::GrblReset,
                              JobResult::Aborted, JobResult::Interrupted}) {
        if (jobResultText(r) == text) return r;
    }
    return JobResult::Aborted;
}

QString logLevelText(LogLevel level)
{
    switch (level) {
    case LogLevel::Info: return QStringLiteral("Info");
    case LogLevel::Warning: return QStringLiteral("Warnung");
    case LogLevel::Error: return QStringLiteral("Fehler");
    }
    return QStringLiteral("Info");
}

LogLevel logLevelFromText(const QString& text)
{
    if (text == QLatin1String("Warnung")) return LogLevel::Warning;
    if (text == QLatin1String("Fehler")) return LogLevel::Error;
    return LogLevel::Info;
}

// ---------------------------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------------------------
QString csvLine(const QStringList& fields)
{
    QStringList out;
    for (const QString& field : fields) {
        if (field.contains(QLatin1Char(';')) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\n')) ||
            field.contains(QLatin1Char('\r'))) {
            QString quoted = field;
            quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            out << QLatin1Char('"') + quoted + QLatin1Char('"');
        } else {
            out << field;
        }
    }
    return out.join(QLatin1Char(';'));
}

QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                current += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(';')) {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

QStringList jobCsvHeader() { return kJobHeader; }
QStringList jobCsvFields(const JobRecord& record) { return jobFieldsImpl(record); }
QStringList systemCsvHeader() { return kSystemHeader; }
QStringList systemCsvFields(const SystemRecord& r)
{
    const QDateTime time = r.time.isValid() ? r.time : QDateTime::currentDateTime();
    return {timeText(time), zoneText(time), logLevelText(r.level), oneLine(r.category), oneLine(r.message), oneLine(r.details)};
}

QString csvNumber(double value, int decimals)
{
    QLocale german(QLocale::German);
    german.setNumberOptions(QLocale::OmitGroupSeparator);   // "2000", nicht "2.000" (das läse ein Tabellenprogramm als 2)
    return german.toString(value, 'f', decimals);
}

double parseCsvNumber(const QString& text)
{
    QString t = text.trimmed();
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    return t.toDouble();
}

// ---------------------------------------------------------------------------------------------
// Auftragsprotokoll
// ---------------------------------------------------------------------------------------------
JobLog::JobLog(const QString& folder) : folder_(folder) {}

QString JobLog::pathFor(const QDateTime& time) const
{
    return QDir(folder_).filePath(QStringLiteral("jobs-%1.csv").arg(monthName(time)));
}

void JobLog::begin(JobRecord& record)
{
    if (!record.start.isValid()) record.start = QDateTime::currentDateTime();
    if (record.id.isEmpty()) record.id = record.start.toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    record.result = JobResult::Running;
    activePath_ = pathFor(record.start);
    activeId_ = record.id;
    activeOffset_ = appendLine(activePath_, kJobHeader, csvLine(jobCsvFields(record)));
}

void JobLog::finish(const JobRecord& record)
{
    const QString line = csvLine(jobCsvFields(record));
    if (record.id == activeId_ && activeOffset_ >= 0) {
        QFile file(activePath_);
        if (file.open(QIODevice::ReadWrite)) {
            // Nur ersetzen, wenn an der gemerkten Stelle wirklich unsere Zeile steht.
            file.seek(activeOffset_);
            const QByteArray start = file.read(record.id.size() + 1);
            if (start == record.id.toUtf8() + ";") {
                file.resize(activeOffset_);
                file.seek(activeOffset_);
                file.write((line + QLatin1Char('\n')).toUtf8());
                file.flush();
                activeId_.clear();
                activeOffset_ = -1;
                return;
            }
        }
    }
    appendLine(record.id == activeId_ && !activePath_.isEmpty() ? activePath_ : pathFor(record.start), kJobHeader, line);
    if (record.id == activeId_) {
        activeId_.clear();
        activeOffset_ = -1;
    }
}

JobRecord JobLog::recoverInterrupted()
{
    const QStringList all = files();
    if (all.isEmpty()) return {};
    const QString path = all.last();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) return {};
    const QByteArray data = file.readAll();
    if (data.isEmpty()) return {};
    // Letzte Zeile samt Anfang finden.
    int end = data.size();
    while (end > 0 && (data[end - 1] == '\n' || data[end - 1] == '\r')) --end;
    const int lineStart = data.lastIndexOf('\n', end - 1) + 1;
    const QString line = QString::fromUtf8(data.mid(lineStart, end - lineStart));
    JobRecord record;
    if (lineStart == 0 || !jobFromFields(parseCsvLine(line), &record) || record.result != JobResult::Running) return {};
    record.result = JobResult::Interrupted;
    record.end = QDateTime();
    record.note = QStringLiteral("Programm beendet oder abgestürzt, bevor der Auftrag zu Ende war");
    file.resize(lineStart);
    file.seek(lineStart);
    file.write((csvLine(jobCsvFields(record)) + QLatin1Char('\n')).toUtf8());
    file.flush();
    return record;
}

QStringList JobLog::files() const
{
    return listFiles(folder_, QStringLiteral("jobs"), QDate(), QDate());
}

QList<JobRecord> JobLog::read(const QDate& from, const QDate& to) const
{
    QMap<QString, int> indexById;
    QList<JobRecord> records;
    for (const QString& path : listFiles(folder_, QStringLiteral("jobs"), from, to)) {
        for (const QString& line : readDataLines(path)) {
            JobRecord record;
            if (!jobFromFields(parseCsvLine(line), &record)) continue;
            if (indexById.contains(record.id)) {
                records[indexById[record.id]] = record;   // die letzte Zeile gilt
            } else {
                indexById[record.id] = static_cast<int>(records.size());
                records.append(record);
            }
        }
    }
    QList<JobRecord> result;
    for (const JobRecord& r : records) {
        const QDate day = r.start.date();
        if (from.isValid() && day < from) continue;
        if (to.isValid() && day > to) continue;
        result.append(r);
    }
    std::stable_sort(result.begin(), result.end(), [](const JobRecord& a, const JobRecord& b) { return a.start < b.start; });
    return result;
}

// ---------------------------------------------------------------------------------------------
// Systemprotokoll
// ---------------------------------------------------------------------------------------------
SystemLog::SystemLog(const QString& folder) : folder_(folder) {}

QString SystemLog::pathFor(const QDateTime& time) const
{
    return QDir(folder_).filePath(QStringLiteral("system-%1.csv").arg(monthName(time)));
}

void SystemLog::append(const SystemRecord& record)
{
    const QDateTime time = record.time.isValid() ? record.time : QDateTime::currentDateTime();
    SystemRecord stamped = record;
    stamped.time = time;
    appendLine(pathFor(time), kSystemHeader, csvLine(systemCsvFields(stamped)));
}

QStringList SystemLog::files() const
{
    return listFiles(folder_, QStringLiteral("system"), QDate(), QDate());
}

QList<SystemRecord> SystemLog::read(const QDate& from, const QDate& to) const
{
    QList<SystemRecord> result;
    for (const QString& path : listFiles(folder_, QStringLiteral("system"), from, to)) {
        for (const QString& line : readDataLines(path)) {
            const QStringList f = parseCsvLine(line);
            if (f.size() < 5) continue;
            SystemRecord r;
            r.time = timeFromText(f[0]);
            if (!r.time.isValid()) continue;
            const QDate day = r.time.date();
            if (from.isValid() && day < from) continue;
            if (to.isValid() && day > to) continue;
            r.level = logLevelFromText(f[2]);
            r.category = f[3];
            r.message = f[4];
            r.details = f.value(5);
            result.append(r);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// Statistik
// ---------------------------------------------------------------------------------------------
JobStats computeJobStats(const QList<JobRecord>& records)
{
    JobStats stats;
    for (const JobRecord& r : records) {
        if (r.result == JobResult::Running) continue;   // läuft noch: zählt erst am Ende
        ++stats.jobs;
        switch (r.result) {
        case JobResult::Finished: ++stats.finished; break;
        case JobResult::CancelledByUser:
        case JobResult::EmergencyStop: ++stats.cancelled; break;
        case JobResult::Interrupted: ++stats.interrupted; break;
        default: ++stats.failed; break;
        }
        stats.activeSeconds += r.activeSeconds;
        stats.runSeconds += r.runSeconds;
        MonthStats& month = stats.months[monthName(r.start)];
        ++month.jobs;
        month.activeSeconds += r.activeSeconds;
        month.runSeconds += r.runSeconds;
    }
    return stats;
}

QString formatHms(qint64 seconds)
{
    if (seconds < 0) seconds = 0;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace ld::core
