#pragma once

#include <QWidget>

// Empty tab with a note, for features of later stages.
class PlaceholderTab : public QWidget
{
    Q_OBJECT
public:
    PlaceholderTab(const QString& title, const QString& text, QWidget* parent = nullptr);
};
