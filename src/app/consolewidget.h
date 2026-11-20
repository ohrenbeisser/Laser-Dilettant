// Konsole: zeigt gesendete und empfangene Zeilen und hat eine Eingabezeile für eigene Befehle
// (siehe docs/planung.md 3.15). Die Statusabfragen von GRBL sind ausgeblendet, solange der Schalter aus ist.
#pragma once

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

namespace ld::app {

class ConsoleWidget : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleWidget(QWidget* parent = nullptr);

    void appendSent(const QString& line);
    void appendReceived(const QString& line);
    void appendInfo(const QString& text);
    void appendError(const QString& text);
    // Eingabe sperren (während eines Auftrags) oder freigeben.
    void setInputEnabled(bool enabled);

protected:
    void changeEvent(QEvent* event) override;

signals:
    void commandEntered(const QString& line);

private:
    // Art einer Zeile; bestimmt die Farbe (abhängig vom Design) und wird im Textblock gemerkt.
    enum Kind { Sent, Received, Info, Error };
    QColor colorFor(int kind) const;
    void append(const QString& text, Kind kind);

    QPlainTextEdit* log_;
    QLineEdit* input_;
    QCheckBox* showStatus_;
};

}  // namespace ld::app
