#pragma once

#include <QStringList>
#include <QWidget>

class GrblDriver;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Command console (section 6.10).
class ConsoleWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConsoleWidget(GrblDriver* driver, QWidget* parent = nullptr);

    void appendInfo(const QString& text);
    void appendError(const QString& text);
    // During a job only real-time commands may be sent.
    void setJobRunning(bool running);
    void setConnected(bool connected);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void send();
    void append(const QString& text, const QString& color = QString());
    void saveLog();

    GrblDriver* m_driver;
    QPlainTextEdit* m_output;
    QLineEdit* m_input;
    QPushButton* m_send;
    QCheckBox* m_showStatus;
    QStringList m_history;
    int m_historyPos = 0;
    bool m_jobRunning = false;
    bool m_connected = false;
};
