#pragma once

#include <QFile>
#include <QObject>

class GrblDriver;

// Writes the communication with the device to a text file (optional).
// Status queries and reports are left out.
class CommLog : public QObject
{
    Q_OBJECT
public:
    CommLog(GrblDriver* driver, QObject* parent = nullptr);

    void setEnabled(bool on, const QString& directory);
    bool isEnabled() const { return m_enabled; }
    QString fileName() const { return m_file.fileName(); }

private:
    void openFile();
    void write(const QString& prefix, const QString& text);

    GrblDriver* m_driver;
    QFile m_file;
    QString m_dir;
    bool m_enabled = false;
};
