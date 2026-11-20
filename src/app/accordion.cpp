#include "accordion.h"

#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace ld::app {

AccordionSection::AccordionSection(const QString& id, const QString& title, bool openByDefault,
                                   QWidget* parent)
    : QWidget(parent), id_(id), title_(title)
{
    // Kopfzeile: flacher, anklickbarer Button. "checkable" = er merkt sich offen/zu.
    header_ = new QPushButton(this);
    header_->setCheckable(true);
    header_->setFlat(true);
    header_->setCursor(Qt::PointingHandCursor);
    header_->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; padding: 6px 10px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: palette(midlight); }"));

    // Inhaltsbereich mit eigenem Layout, in das die Bedienelemente eingefügt werden.
    body_ = new QWidget(this);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(10, 2, 10, 10);
    bodyLayout_->setSpacing(6);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(body_);

    // Gemerkten Zustand lesen (Standard: openByDefault).
    const bool open =
        QSettings().value(QStringLiteral("accordion/") + id_, openByDefault).toBool();
    header_->setChecked(open);
    body_->setVisible(open);
    updateHeader();

    // Klick auf die Kopfzeile schaltet um.
    connect(header_, &QPushButton::toggled, this, &AccordionSection::setOpen);
}

bool AccordionSection::isOpen() const
{
    return header_->isChecked();
}

void AccordionSection::setOpen(bool open)
{
    // Falls von außen aufgerufen: Button an den Zustand angleichen (ohne Endlosschleife,
    // denn setChecked löst nur dann ein Signal aus, wenn sich der Wert ändert).
    if (header_->isChecked() != open) {
        header_->setChecked(open);
        return;
    }
    body_->setVisible(open);
    updateHeader();
    QSettings().setValue(QStringLiteral("accordion/") + id_, open);
}

void AccordionSection::updateHeader()
{
    // ▾ = offen, ▸ = zu
    header_->setText((isOpen() ? QStringLiteral("▾  ") : QStringLiteral("▸  ")) + title_);
}

}  // namespace ld::app
