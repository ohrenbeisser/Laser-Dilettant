#include "device/CommLog.h"

#include "device/GrblDriver.h"

#include <QDateTime>
#include <QDir>

CommLog::CommLog(GrblDriver* driver, QObject* parent)
    : QObject(parent)
    , m_driver(driver)
{
    connect(driver, &GrblDriver::bytesWritten, this, [this](const QByteArray& data) {
        if (!m_enabled || data == "?")
            return;
        QString text;
        for (char c : data) {
            const auto u = static_cast<unsigned char>(c);
            if (c == '\n') {
                write(QStringLiteral("> "), text);
                text.clear();
            } else if (u < 0x20 || u >= 0x7F) {
                write(QStringLiteral("> "), QStringLiteral("[0x%1]").arg(u, 2, 16, QLatin1Char('0')));
            } else {
                text += QLatin1Char(c);
            }
        }
        if (!text.isEmpty())
            write(QStringLiteral("> "), text);
    });
    connect(driver, &GrblDriver::lineReceived, this, [this](const QString& line, bool isStatus) {
        if (m_enabled && !isStatus)
            write(QStringLiteral("< "), line);
    });
    connect(driver, &GrblDriver::connected, this, [this] {
        if (m_enabled)
            openFile();
    });
}

void CommLog::setEnabled(bool on, const QString& directory)
{
    m_dir = directory;
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on)
        openFile();
    else
        m_file.close();
}

void CommLog::openFile()
{
    m_file.close();
    QDir().mkpath(m_dir);
    m_file.setFileName(m_dir + QStringLiteral("/comm-%1.log")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        m_enabled = false;
}

void CommLog::write(const QString& prefix, const QString& text)
{
    if (!m_file.isOpen())
        openFile();
    if (!m_file.isOpen())
        return;
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) + prefix + text + QLatin1Char('\n');
    m_file.write(line.toUtf8());
    m_file.flush();
}
