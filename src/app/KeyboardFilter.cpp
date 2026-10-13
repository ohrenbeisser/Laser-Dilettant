#include "app/KeyboardFilter.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTextEdit>
#include <QWidget>

KeyboardFilter::KeyboardFilter(QObject* parent)
    : QObject(parent)
{
}

bool KeyboardFilter::isTextInput(QWidget* w)
{
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QPlainTextEdit*>(w)
        || qobject_cast<QTextEdit*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QTableView*>(w);
}

bool KeyboardFilter::focusInJogWidget() const
{
    QWidget* f = QApplication::focusWidget();
    if (!f || isTextInput(f))
        return false;
    for (QWidget* w : jogWidgets)
        if (w && (f == w || w->isAncestorOf(f)))
            return true;
    return false;
}

bool KeyboardFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return QObject::eventFilter(watched, event);
    // Only the event for the widget that has the focus counts; key events are
    // also delivered to parents and windows.
    if (!watched->isWidgetType())
        return QObject::eventFilter(watched, event);
    auto* ke = static_cast<QKeyEvent*>(event);

    if (ke->key() == Qt::Key_Escape) {
        if (event->type() == QEvent::KeyPress && !ke->isAutoRepeat()) {
            // The same key press may pass through several widgets.
            if (!m_lastEscape.isValid() || m_lastEscape.elapsed() > 300) {
                m_lastEscape.restart();
                if (onEscape)
                    onEscape();
            }
        }
        // Let dialogs still close with Esc.
        return QObject::eventFilter(watched, event);
    }

    if (event->type() != QEvent::KeyPress || ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        return QObject::eventFilter(watched, event);
    if (watched != QApplication::focusWidget())
        return QObject::eventFilter(watched, event);

    if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()) {
        QWidget* f = QApplication::focusWidget();
        if (!isTextInput(f) && onSpace && onSpace())
            return true;
        return QObject::eventFilter(watched, event);
    }

    if (!focusInJogWidget())
        return QObject::eventFilter(watched, event);

    int sx = 0, sy = 0;
    switch (ke->key()) {
    case Qt::Key_Left: sx = -1; break;
    case Qt::Key_Right: sx = 1; break;
    case Qt::Key_Up: sy = 1; break;
    case Qt::Key_Down: sy = -1; break;
    case Qt::Key_PageUp:
        if (onPage)
            onPage(1);
        return true;
    case Qt::Key_PageDown:
        if (onPage)
            onPage(-1);
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
    // Autorepeat would flood the planner with jog commands.
    if (!ke->isAutoRepeat() && onArrow)
        onArrow(sx, sy);
    return true;
}
