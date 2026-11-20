#include "consolewidget.h"

#include "appsettings.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QEvent>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace ld::app {

ConsoleWidget::ConsoleWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(AppSettings::instance().consoleMaxLines());
    // Hintergrund und Grundfarbe kommen aus der Palette (folgen dem Design); nur die Schrift ist fest.
    log_->setStyleSheet(QStringLiteral("QPlainTextEdit { font-family: monospace; }"));
    layout->addWidget(log_, 1);

    auto* row = new QHBoxLayout;
    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Befehl eingeben (Enter sendet), zum Beispiel $$ oder G0 X10"));
    showStatus_ = new QCheckBox(tr("Statusabfragen anzeigen"));
    showStatus_->setChecked(AppSettings::instance().consoleShowStatus());   // Vorgabe aus den Einstellungen
    row->addWidget(input_, 1);
    row->addWidget(showStatus_);
    layout->addLayout(row);

    // Ändert der Bediener die Konsole in den Einstellungen, gilt das sofort.
    connect(&AppSettings::instance(), &AppSettings::consoleChanged, this, [this] {
        log_->setMaximumBlockCount(AppSettings::instance().consoleMaxLines());
        showStatus_->setChecked(AppSettings::instance().consoleShowStatus());
    });
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const QString text = input_->text().trimmed();
        if (text.isEmpty()) return;
        input_->clear();
        emit commandEntered(text);
    });
    appendInfo(tr("Konsole: noch keine Verbindung zum Laser."));
}

QColor ConsoleWidget::colorFor(int kind) const
{
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    switch (kind) {
    case Sent: return dark ? QColor(0x7a, 0xc0, 0xff) : QColor(0x0a, 0x58, 0xb8);
    case Received: return dark ? QColor(0xb8, 0xf2, 0xc8) : QColor(0x1d, 0x7a, 0x3a);
    case Error: return dark ? QColor(0xff, 0x7a, 0x7a) : QColor(0xc0, 0x20, 0x1f);
    default: return dark ? QColor(0xaa, 0xaa, 0xaa) : QColor(0x66, 0x66, 0x66);
    }
}

void ConsoleWidget::append(const QString& text, Kind kind)
{
    QTextCharFormat format;
    format.setForeground(colorFor(kind));
    QTextCursor cursor(log_->document());
    cursor.movePosition(QTextCursor::End);
    if (!log_->document()->isEmpty()) cursor.insertBlock();
    cursor.insertText(text, format);
    cursor.block().setUserState(kind);   // die Art bleibt am Textblock, damit ein Designwechsel umfärben kann
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

// Designwechsel: die vorhandenen Zeilen bekommen die Farben des neuen Designs.
void ConsoleWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange) return;
    for (QTextBlock block = log_->document()->begin(); block.isValid(); block = block.next()) {
        QTextCharFormat format;
        format.setForeground(colorFor(block.userState()));
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(format);
    }
}

void ConsoleWidget::appendSent(const QString& line) { append(QStringLiteral("> ") + line, Sent); }

void ConsoleWidget::appendReceived(const QString& line)
{
    if (line.startsWith(QLatin1Char('<')) && !showStatus_->isChecked()) return;
    append(line, Received);
}

void ConsoleWidget::appendInfo(const QString& text) { append(text, Info); }
void ConsoleWidget::appendError(const QString& text) { append(text, Error); }

void ConsoleWidget::setInputEnabled(bool enabled)
{
    input_->setEnabled(enabled);
    input_->setPlaceholderText(enabled ? tr("Befehl eingeben (Enter sendet), zum Beispiel $$ oder G0 X10")
                                       : tr("Während des Auftrags gesperrt"));
}

}  // namespace ld::app
