#pragma once

#include <QWidget>

class QToolButton;
class QVBoxLayout;

// A titled section that can be expanded and collapsed.
class CollapsibleSection : public QWidget
{
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, QWidget* content, bool expanded = false, QWidget* parent = nullptr);

    void setExpanded(bool on);
    bool isExpanded() const;
    QWidget* content() const { return m_content; }

private:
    QToolButton* m_header;
    QWidget* m_content;
};
