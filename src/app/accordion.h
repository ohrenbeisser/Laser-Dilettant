// Einklappbarer Abschnitt ("Akkordeon") für die Seitenleisten.
//
// Qt hat kein fertiges Akkordeon. Dieses Widget besteht aus einer Kopfzeile (Button mit
// Pfeil und Titel) und einem Inhaltsbereich, der ein- und ausgeblendet wird.
// Ob ein Abschnitt offen ist, wird in den Programmeinstellungen (QSettings) gemerkt.
#pragma once

#include <QString>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace ld::app {

class AccordionSection : public QWidget {
    Q_OBJECT

public:
    // id:            eindeutiger Schlüssel für die Einstellungen (z. B. "left/connection")
    // title:         Überschrift der Kopfzeile
    // openByDefault: Zustand beim allerersten Start (danach gilt der gemerkte Zustand)
    AccordionSection(const QString& id, const QString& title, bool openByDefault,
                     QWidget* parent = nullptr);

    // Hier kommen die Bedienelemente des Abschnitts hinein.
    QVBoxLayout* contentLayout() const { return bodyLayout_; }

    bool isOpen() const;

public slots:
    void setOpen(bool open);

private:
    void updateHeader();

    QString id_;
    QString title_;
    QPushButton* header_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
};

}  // namespace ld::app
