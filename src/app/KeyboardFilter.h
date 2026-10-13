#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <functional>

// Application-wide keyboard handling (section 4.6):
//  - Esc: emergency stop, always, even while an input field has the focus
//  - Space: pause/resume, only if no input field has the focus
//  - Arrow keys / Page Up / Page Down: jog / step size, only if the focus is
//    inside one of the "jog widgets" (work area, control tab)
class KeyboardFilter : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardFilter(QObject* parent = nullptr);

    std::function<void()> onEscape;
    std::function<bool()> onSpace;                 // returns true if handled
    std::function<void(int sx, int sy)> onArrow;
    std::function<void(int direction)> onPage;
    QList<QWidget*> jogWidgets;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool focusInJogWidget() const;
    static bool isTextInput(QWidget* w);

    QElapsedTimer m_lastEscape;
};
