#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Runs avrdude to read or write the flash memory of an ATmega328P
// (Arduino Nano). The serial port must be closed by the program first.
class AvrdudeRunner : public QObject
{
    Q_OBJECT
public:
    explicit AvrdudeRunner(QObject* parent = nullptr);
    ~AvrdudeRunner() override;

    // Searches for avrdude: configured path, next to the executable
    // (Windows package), then PATH. Returns an empty string if not found.
    static QString findAvrdude(const QString& configuredPath = QString());

    // Port name as avrdude expects it (Windows: "COM3" instead of the device path).
    static QString avrdudePortName(const QString& port);

    static QStringList readFlashArguments(const QString& port, int bootBaud, const QString& hexFile);
    static QStringList writeFlashArguments(const QString& port, int bootBaud, const QString& hexFile);

    void setProgram(const QString& path) { m_program = path; }
    QString program() const { return m_program; }

    bool backupFlash(const QString& port, int bootBaud, const QString& hexFile);
    bool writeFlash(const QString& port, int bootBaud, const QString& hexFile);
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
    void cancel();

    // Basic check of an Intel HEX file (record syntax and checksums).
    static bool validateHexFile(const QString& path, QString* error);

signals:
    void output(const QString& text);
    void finished(bool success, const QString& message);

private:
    bool start(const QStringList& args);

    QProcess m_process;
    QString m_program;
};
