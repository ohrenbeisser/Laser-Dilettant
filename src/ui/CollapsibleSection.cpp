#include "ui/CollapsibleSection.h"

#include <QFrame>
#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* content, bool expanded, QWidget* parent)
    : QWidget(parent)
    , m_header(new QToolButton(this))
    , m_content(content)
{
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setAutoRaise(true);
    QFont f = m_header->font();
    f.setBold(true);
    m_header->setFont(f);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_header);
    layout->addWidget(line);
    layout->addWidget(m_content);
    m_content->setParent(this);

    connect(m_header, &QToolButton::toggled, this, &CollapsibleSection::setExpanded);
    setExpanded(expanded);
}

void CollapsibleSection::setExpanded(bool on)
{
    m_header->setChecked(on);
    m_header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_content->setVisible(on);
}

bool CollapsibleSection::isExpanded() const
{
    return m_header->isChecked();
}
