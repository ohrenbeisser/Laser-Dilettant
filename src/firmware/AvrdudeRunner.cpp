#include "firmware/AvrdudeRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

AvrdudeRunner::AvrdudeRunner(QObject* parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this,
            [this] { emit output(QString::fromLocal8Bit(m_process.readAll())); });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        emit finished(ok, ok ? tr("avrdude finished successfully.")
                             : tr("avrdude failed (exit code %1).").arg(exitCode));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, tr("avrdude could not be started: %1").arg(m_process.errorString()));
    });
}

AvrdudeRunner::~AvrdudeRunner()
{
    if (isRunning()) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

QString AvrdudeRunner::findAvrdude(const QString& configuredPath)
{
    if (!configuredPath.isEmpty() && QFileInfo(configuredPath).isExecutable())
        return configuredPath;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList local = { appDir + QStringLiteral("/avrdude/avrdude"), appDir + QStringLiteral("/avrdude") };
    for (QString candidate : local) {
#ifdef Q_OS_WIN
        candidate += QStringLiteral(".exe");
#endif
        if (QFileInfo(candidate).isExecutable() && QFileInfo(candidate).isFile())
            return candidate;
    }
    return QStandardPaths::findExecutable(QStringLiteral("avrdude"));
}

QString AvrdudeRunner::avrdudePortName(const QString& port)
{
    QString p = port;
    if (p.startsWith(QLatin1String("\\\\.\\")))
        p = p.mid(4);
    return p;
}

QStringList AvrdudeRunner::readFlashArguments(const QString& port, int bootBaud, const QString& hexFile)
{
    return { QStringLiteral("-c"), QStringLiteral("arduino"), QStringLiteral("-p"), QStringLiteral("m328p"),
             QStringLiteral("-P"), avrdudePortName(port), QStringLiteral("-b"), QString::number(bootBaud),
             QStringLiteral("-U"), QStringLiteral("flash:r:%1:i").arg(QDir::toNativeSeparators(hexFile)) };
}

QStringList AvrdudeRunner::writeFlashArguments(const QString& port, int bootBaud, const QString& hexFile)
{
    return { QStringLiteral("-c"), QStringLiteral("arduino"), QStringLiteral("-p"), QStringLiteral("m328p"),
             QStringLiteral("-P"), avrdudePortName(port), QStringLiteral("-b"), QString::number(bootBaud),
             QStringLiteral("-D"),
             QStringLiteral("-U"), QStringLiteral("flash:w:%1:i").arg(QDir::toNativeSeparators(hexFile)) };
}

bool AvrdudeRunner::start(const QStringList& args)
{
    if (isRunning())
        return false;
    if (m_program.isEmpty())
        m_program = findAvrdude();
    if (m_program.isEmpty()) {
        emit finished(false, tr("avrdude was not found."));
        return false;
    }
    emit output(QStringLiteral("$ %1 %2\n").arg(m_program, args.join(QLatin1Char(' '))));
    m_process.start(m_program, args);
    return true;
}

bool AvrdudeRunner::backupFlash(const QString& port, int bootBaud, const QString& hexFile)
{
    return start(readFlashArguments(port, bootBaud, hexFile));
}

bool AvrdudeRunner::writeFlash(const QString& port, int bootBaud, const QString& hexFile)
{
    QString error;
    if (!validateHexFile(hexFile, &error)) {
        emit finished(false, error);
        return false;
    }
    return start(writeFlashArguments(port, bootBaud, hexFile));
}

void AvrdudeRunner::cancel()
{
    if (isRunning())
        m_process.kill();
}

bool AvrdudeRunner::validateHexFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = tr("The file %1 cannot be read.").arg(path);
        return false;
    }
    int lineNo = 0;
    bool sawEof = false;
    int dataBytes = 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty())
            continue;
        const QByteArray bytes = QByteArray::fromHex(line.mid(1));
        if (!line.startsWith(':') || line.size() < 11 || (line.size() - 1) % 2 != 0
            || bytes.size() != (line.size() - 1) / 2) {
            if (error)
                *error = tr("Line %1 is not a valid Intel HEX record.").arg(lineNo);
            return false;
        }
        quint8 sum = 0;
        for (char b : bytes)
            sum = static_cast<quint8>(sum + static_cast<quint8>(b));
        const int count = static_cast<quint8>(bytes[0]);
        if (sum != 0 || bytes.size() != count + 5) {
            if (error)
                *error = tr("Checksum error in line %1.").arg(lineNo);
            return false;
        }
        const int type = static_cast<quint8>(bytes[3]);
        if (type == 0)
            dataBytes += count;
        if (type == 1)
            sawEof = true;
    }
    if (!sawEof || dataBytes == 0) {
        if (error)
            *error = tr("The file is incomplete (no end record or no data).");
        return false;
    }
    // ATmega328P: 32 KiB flash, the boot loader needs the last 0.5-2 KiB.
    if (dataBytes > 32768) {
        if (error)
            *error = tr("The firmware is too large for an ATmega328P.");
        return false;
    }
    return true;
}
