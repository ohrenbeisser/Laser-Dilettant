// Protokolle (siehe docs/planung.md 3.38): Auftragsprotokoll und Systemprotokoll als CSV-Dateien, je Monat eine Datei.
//
//   jobs-JJJJ-MM.csv    Ein Auftrag pro Zeile (Start, Ende, Name, Einstellungen, Ergebnis, Laufzeit, Arbeitszeit ...)
//   system-JJJJ-MM.csv  Ein Ereignis pro Zeile (App gestartet, Verbunden, Alarm, Fehler, NOT-AUS ...)
//
// Die Dateien sind für Tabellenprogramme gedacht: Trennzeichen Semikolon, UTF-8 mit BOM, Zahlen mit Komma, Zeiten als
// "JJJJ-MM-TT hh:mm:ss" in Ortszeit mit der Zeitzone in einer eigenen Spalte. Sie werden nie automatisch gelöscht.
// Ein laufender Auftrag steht schon mit dem Ergebnis "läuft" als letzte Zeile der Datei; am Ende wird genau diese Zeile
// ersetzt. Fehlt das Ende (Absturz, Stromausfall), markiert recoverInterrupted() ihn beim nächsten Start als
// "unterbrochen". Dieser Teil kennt keine Oberfläche und keinen Laser; er ist deshalb ohne Fenster testbar.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace ld::core {

enum class JobResult {
    Running,           // läuft gerade
    Finished,          // fertig
    CancelledByUser,   // vom Bediener abgebrochen
    EmergencyStop,     // NOT-AUS
    Alarm,             // Alarm von GRBL
    Error,             // Fehler im Auftrag (GRBL-Fehlermeldung, vom Bediener beendet)
    ConnectionLost,    // Verbindung zum Laser verloren oder getrennt
    GrblReset,         // GRBL wurde zurückgesetzt (zum Beispiel Stromausfall des Lasers)
    Aborted,           // anderer Abbruch
    Interrupted,       // Programm beendet oder abgestürzt, bevor der Auftrag zu Ende war
};
QString jobResultText(JobResult result);                 // deutscher Text, wie er in der CSV steht
JobResult jobResultFromText(const QString& text);        // unbekannter Text: Aborted

struct JobRecord {
    QString id;                    // eindeutig, aus dem Startzeitpunkt
    QDateTime start;
    QDateTime end;                 // ungültig, solange der Auftrag läuft oder unterbrochen wurde
    QString name;                  // Bild- oder Dateiname, Text, "Kalibrierung", "Rahmen", "Materialtest"
    QString kind;                  // Bild, Text, Kalibrierung, NC-Datei, Rahmen, Materialtest
    QString mode;                  // Full, Cutting, Linie (leer bei NC-Dateien)
    double speedMmPerMin = 0.0;
    double powerPercent = 0.0;
    int passes = 1;
    double widthMm = 0.0;
    double heightMm = 0.0;
    QString material;
    QString profile;               // Maschinenprofil
    JobResult result = JobResult::Running;
    int runSeconds = 0;            // Laufzeit (Uhr, von Start bis Ende, mit Pausen)
    int activeSeconds = 0;         // Arbeitszeit (GRBL im Zustand "Läuft", ohne Pausen)
    int estimateSeconds = 0;       // geschätzte Dauer (vor dem Start)
    int lines = 0;                 // von GRBL bestätigte Zeilen
    QString note;                  // zum Beispiel Fehlermeldung oder Grund des Abbruchs
};

enum class LogLevel { Info, Warning, Error };
QString logLevelText(LogLevel level);
LogLevel logLevelFromText(const QString& text);

struct SystemRecord {
    QDateTime time;
    LogLevel level = LogLevel::Info;
    QString category;              // App, Verbindung, Alarm, Sicherheit, Auftrag, Profil ...
    QString message;
    QString details;
};

// ---- CSV-Hilfen (auch für Tests und den Export) ----
QString csvLine(const QStringList& fields);              // mit Semikolon, Felder bei Bedarf in Anführungszeichen
QStringList parseCsvLine(const QString& line);           // Gegenstück zu csvLine
QStringList jobCsvHeader();                              // Spaltenköpfe der Auftragsprotokolle
QStringList jobCsvFields(const JobRecord& record);       // eine Zeile (für Datei und Export)
QStringList systemCsvHeader();
QStringList systemCsvFields(const SystemRecord& record);
QString csvNumber(double value, int decimals);           // Zahl mit Komma (unabhängig von der Systemsprache)
double parseCsvNumber(const QString& text);              // liest Komma und Punkt

// ---- Auftragsprotokoll ----
class JobLog {
public:
    explicit JobLog(const QString& folder);

    // Neuer Auftrag: schreibt die Zeile mit dem Ergebnis "läuft" und merkt sich ihre Stelle. Setzt record.id, falls leer.
    void begin(JobRecord& record);
    // Auftrag zu Ende: ersetzt die Zeile von begin() durch den Endstand (fehlt sie, wird eine neue angehängt).
    void finish(const JobRecord& record);
    // Beim Programmstart: Ist die letzte Zeile der neuesten Datei noch "läuft", wird sie "unterbrochen". Gibt den
    // betroffenen Auftrag zurück (id leer: es gab nichts zu tun).
    JobRecord recoverInterrupted();
    // Alle Aufträge im Zeitraum (ungültige Grenze = offen), älteste zuerst. Doppelte IDs: die letzte Zeile gilt.
    QList<JobRecord> read(const QDate& from = QDate(), const QDate& to = QDate()) const;
    QStringList files() const;     // alle Dateien, älteste zuerst
    QString folder() const { return folder_; }

private:
    QString pathFor(const QDateTime& time) const;
    QString folder_;
    QString activePath_;
    QString activeId_;
    qint64 activeOffset_ = -1;
};

// ---- Systemprotokoll ----
class SystemLog {
public:
    explicit SystemLog(const QString& folder);

    void append(const SystemRecord& record);
    // Alle Ereignisse im Zeitraum, älteste zuerst.
    QList<SystemRecord> read(const QDate& from = QDate(), const QDate& to = QDate()) const;
    QStringList files() const;
    QString folder() const { return folder_; }

private:
    QString pathFor(const QDateTime& time) const;
    QString folder_;
};

// ---- Statistik aus dem Auftragsprotokoll ----
struct MonthStats {
    int jobs = 0;
    qint64 activeSeconds = 0;
    qint64 runSeconds = 0;
};

struct JobStats {
    int jobs = 0;
    int finished = 0;
    int cancelled = 0;             // vom Bediener, NOT-AUS
    int failed = 0;                // Alarm, Fehler, Verbindung verloren, GRBL-Reset, anderer Abbruch
    int interrupted = 0;
    qint64 activeSeconds = 0;      // Gesamtbetriebszeit (Arbeitszeit aller Aufträge)
    qint64 runSeconds = 0;
    QMap<QString, MonthStats> months;   // "JJJJ-MM" -> Summe
};
JobStats computeJobStats(const QList<JobRecord>& records);

QString formatHms(qint64 seconds);   // "h:mm:ss" (Stunden auch über 24)

}  // namespace ld::core
