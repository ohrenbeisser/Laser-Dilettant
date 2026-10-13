#include "ui/tabs/PlaceholderTab.h"

#include <QLabel>
#include <QVBoxLayout>

PlaceholderTab::PlaceholderTab(const QString& title, const QString& text, QWidget* parent)
    : QWidget(parent)
{
    auto* heading = new QLabel(title, this);
    QFont f = heading->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.3);
    heading->setFont(f);
    heading->setAlignment(Qt::AlignCenter);
    auto* body = new QLabel(text, this);
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignCenter);
    auto* l = new QVBoxLayout(this);
    l->addStretch(1);
    l->addWidget(heading);
    l->addWidget(body);
    l->addStretch(2);
}
