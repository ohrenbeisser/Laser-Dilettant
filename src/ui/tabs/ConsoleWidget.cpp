#include "ui/tabs/ConsoleWidget.h"

#include "device/GrblCodes.h"
#include "device/GrblDriver.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QVBoxLayout>

ConsoleWidget::ConsoleWidget(GrblDriver* driver, QWidget* parent)
    : QWidget(parent)
    , m_driver(driver)
    , m_output(new QPlainTextEdit(this))
    , m_input(new QLineEdit(this))
    , m_send(new QPushButton(tr("Send"), this))
    , m_showStatus(new QCheckBox(tr("Show status reports"), this))
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(5000);
    m_output->setMinimumHeight(120);
    m_input->setFont(mono);
    m_input->setPlaceholderText(tr("Command, e.g. $$ or G0 X10 (↑/↓: history)"));
    m_input->installEventFilter(this);

    auto* clear = new QPushButton(tr("Clear"), this);
    auto* save = new QPushButton(tr("Save log…"), this);

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_send);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_showStatus);
    buttons->addStretch(1);
    buttons->addWidget(clear);
    buttons->addWidget(save);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_output, 1);
    l->addLayout(inputRow);
    l->addLayout(buttons);

    connect(m_input, &QLineEdit::returnPressed, this, &ConsoleWidget::send);
    connect(m_send, &QPushButton::clicked, this, &ConsoleWidget::send);
    connect(clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    connect(save, &QPushButton::clicked, this, &ConsoleWidget::saveLog);

    connect(driver, &GrblDriver::lineSent, this, [this](const QString& line) { append(QStringLiteral("> ") + line); });
    connect(driver, &GrblDriver::lineReceived, this, [this](const QString& line, bool isStatus) {
        if (isStatus) {
            if (m_showStatus->isChecked())
                append(line, QStringLiteral("#808080"));
            return;
        }
        if (line.startsWith(QLatin1String("error:"))) {
            const int code = line.mid(6).toInt();
            append(QStringLiteral("%1  (%2)").arg(line, GrblCodes::errorShort(code)), QStringLiteral("#e53935"));
        } else if (line.startsWith(QLatin1String("ALARM:"))) {
            const int code = line.mid(6).toInt();
            append(QStringLiteral("%1  (%2)").arg(line, GrblCodes::alarmShort(code)), QStringLiteral("#e53935"));
        } else {
            append(line);
        }
    });
    setConnected(false);
}

bool ConsoleWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Up && !m_history.isEmpty()) {
            m_historyPos = qMax(0, m_historyPos - 1);
            m_input->setText(m_history.value(m_historyPos));
            return true;
        }
        if (ke->key() == Qt::Key_Down && !m_history.isEmpty()) {
            m_historyPos = qMin(int(m_history.size()), m_historyPos + 1);
            m_input->setText(m_history.value(m_historyPos));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ConsoleWidget::send()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    if (m_history.isEmpty() || m_history.last() != text)
        m_history << text;
    if (m_history.size() > 100)
        m_history.removeFirst();
    m_historyPos = m_history.size();
    m_input->clear();

    // Real-time commands are sent without line end.
    char realtime = 0;
    if (text == QLatin1String("?"))
        realtime = GrblDriver::StatusQuery;
    else if (text == QLatin1String("!"))
        realtime = GrblDriver::FeedHold;
    else if (text == QLatin1String("~"))
        realtime = GrblDriver::CycleStart;
    else if (text.compare(QLatin1String("ctrl-x"), Qt::CaseInsensitive) == 0 || text == QLatin1String("^X"))
        realtime = GrblDriver::SoftReset;
    if (realtime) {
        append(QStringLiteral("> %1  (%2)").arg(text, tr("real-time command")));
        m_driver->sendRealtime(realtime);
        return;
    }
    if (m_jobRunning) {
        appendError(tr("While a job is running only the real-time commands ? ! ~ ctrl-x can be sent."));
        return;
    }
    if (!m_driver->isConnected()) {
        appendError(tr("Not connected."));
        return;
    }
    m_driver->sendCommand(text, [this, text](bool ok, const QString& r) {
        if (!ok) {
            const int code = r.mid(6).toInt();
            appendError(tr("\"%1\" was rejected: %2").arg(text, GrblCodes::errorText(code)));
        }
    });
}

void ConsoleWidget::append(const QString& text, const QString& color)
{
    QScrollBar* sb = m_output->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    if (color.isEmpty())
        m_output->appendPlainText(text);
    else
        m_output->appendHtml(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text.toHtmlEscaped()));
    if (atBottom)
        sb->setValue(sb->maximum());
}

void ConsoleWidget::appendInfo(const QString& text)
{
    append(QStringLiteral("# ") + text, QStringLiteral("#1e88e5"));
}

void ConsoleWidget::appendError(const QString& text)
{
    append(QStringLiteral("# ") + text, QStringLiteral("#e53935"));
}

void ConsoleWidget::setJobRunning(bool running)
{
    m_jobRunning = running;
}

void ConsoleWidget::setConnected(bool connected)
{
    m_connected = connected;
    m_send->setEnabled(connected);
}

void ConsoleWidget::saveLog()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save console log"), QStringLiteral("console.log"),
                                                      tr("Text files (*.log *.txt)"));
    if (file.isEmpty())
        return;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save console log"), f.errorString());
        return;
    }
    f.write(m_output->toPlainText().toUtf8());
    f.write("\n");
    if (!f.commit())
        QMessageBox::warning(this, tr("Save console log"), f.errorString());
}
