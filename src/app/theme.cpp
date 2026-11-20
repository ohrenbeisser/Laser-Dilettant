#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

namespace ld::app {

QPalette darkPalette()
{
    // Farben wie im dunklen Mockup: Panels #2a2a2c, Eingabefelder #1e1e1f, Arbeitsfläche #18181a.
    const QColor window(0x2a, 0x2a, 0x2c);
    const QColor base(0x1e, 0x1e, 0x1f);
    const QColor canvas(0x18, 0x18, 0x1a);
    const QColor button(0x32, 0x32, 0x35);
    const QColor line(0x45, 0x45, 0x49);
    const QColor text(0xec, 0xec, 0xee);
    const QColor muted(0x9a, 0x9a, 0xa2);
    const QColor disabled(0x6e, 0x6e, 0x75);
    const QColor highlight(0x2f, 0x6f, 0xd0);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, canvas);   // die Arbeitsfläche malt damit ihren Grund
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x4a, 0x4a, 0x4f));
    p.setColor(QPalette::Midlight, QColor(0x3b, 0x3b, 0x3f));
    p.setColor(QPalette::Mid, line);
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1c));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Link, QColor(0x6a, 0xa5, 0xff));
    p.setColor(QPalette::LinkVisited, QColor(0xb0, 0x8c, 0xff));
    // Ausgegraute Elemente
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, disabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x45, 0x45, 0x49));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

QPalette lightPalette()
{
    // Ähnlich der Standardpalette des Fusion-Stils, aber fest, und mit demselben Blau wie das dunkle Design.
    const QColor text(0x1e, 0x1e, 0x1f);
    const QColor disabled(0x8a, 0x8a, 0x90);
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xef, 0xef, 0xef));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xef, 0xef, 0xef));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe4, 0xe4, 0xe7));
    p.setColor(QPalette::Mid, QColor(0xb8, 0xb8, 0xb8));
    p.setColor(QPalette::Dark, QColor(0x9f, 0x9f, 0x9f));
    p.setColor(QPalette::Shadow, QColor(0x76, 0x76, 0x76));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd0));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x6b, 0x6b, 0x72));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Link, QColor(0x1a, 0x5c, 0xc0));
    p.setColor(QPalette::LinkVisited, QColor(0x6a, 0x3c, 0xb0));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, disabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0xc8, 0xc8, 0xcc));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

QIcon themeIcon(bool sun, const QColor& color)
{
    // Das Symbol wird in doppelter Auflösung gezeichnet, damit es auf hochauflösenden Bildschirmen scharf ist.
    const int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(size / 2.0, size / 2.0);
    if (sun) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(center, size * 0.20, size * 0.20);
        painter.setPen(QPen(color, size * 0.07, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 8; ++i) {
            painter.save();
            painter.translate(center);
            painter.rotate(i * 45.0);
            painter.drawLine(QPointF(0, -size * 0.31), QPointF(0, -size * 0.44));
            painter.restore();
        }
    } else {
        // Mondsichel: großer Kreis minus ein versetzter Kreis
        QPainterPath moon;
        moon.addEllipse(center, size * 0.42, size * 0.42);
        QPainterPath cut;
        cut.addEllipse(center + QPointF(size * 0.20, -size * 0.14), size * 0.34, size * 0.34);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(moon.subtracted(cut));
    }
    painter.end();
    return QIcon(pixmap);
}

bool applyTheme(DesignChoice choice)
{
    QStyleHints* hints = QGuiApplication::styleHints();
    bool dark = false;
    switch (choice) {
    case DesignChoice::Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);   // färbt auf Windows/macOS auch die Titelleiste
        dark = true;
        break;
    case DesignChoice::Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case DesignChoice::System:
        hints->unsetColorScheme();
        dark = hints->colorScheme() == Qt::ColorScheme::Dark;
        break;
    }
    QApplication::setPalette(dark ? darkPalette() : lightPalette());
    // Widgets mit eigenem Stylesheet übernehmen eine neue Palette nicht von selbst: Stylesheet neu setzen.
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget->styleSheet().isEmpty()) widget->setStyleSheet(widget->styleSheet());
    }
    return dark;
}

}  // namespace ld::app
