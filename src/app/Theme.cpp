#include "app/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace Theme {

namespace {

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x2b, 0x2d, 0x30);
    const QColor base(0x1e, 0x1f, 0x22);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x80, 0x80, 0x80);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0x64, 0xb5, 0xf6));
    p.setColor(QPalette::Highlight, QColor(0x1e, 0x88, 0xe5));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Mid, QColor(0x3c, 0x3f, 0x41));
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x18));
    return p;
}

QPalette lightPalette()
{
    QPalette p = QStyleFactory::create(QStringLiteral("Fusion"))->standardPalette();
    return p;
}

} // namespace

void apply(AppSettings::Theme theme)
{
    static const QPalette systemPalette = QApplication::palette();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    switch (theme) {
    case AppSettings::Theme::Dark:
        QApplication::setPalette(darkPalette());
        break;
    case AppSettings::Theme::Light:
        QApplication::setPalette(lightPalette());
        break;
    case AppSettings::Theme::System:
        QApplication::setPalette(systemPalette);
        break;
    }
}

} // namespace Theme
