// Knopf "Nullpunkt setzen" hinter den Feldern Position X und Y im rechten Panel (siehe docs/planung.md 3.24).
// Wie beim Schloss hinter Breite und Höhe verbindet eine Klammer die beiden Felder; in der Mitte sitzt ein
// Fadenkreuz mit Kreis (das Zeichen des Nullpunkts auf der Arbeitsfläche). Ein Klick setzt die Grafik mit ihrem
// Job-Ursprung auf den Nullpunkt. Das Symbol wird selbst gezeichnet und folgt der Palette (Hell/Dunkel).
#pragma once

#include <QPainter>
#include <QToolButton>

namespace ld::app {

class ZeroButton : public QToolButton {
public:
    explicit ZeroButton(QWidget* parent = nullptr) : QToolButton(parent)
    {
        setFixedWidth(34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("Nullpunkt setzen: Die Grafik mit ihrem Job-Ursprung auf den Nullpunkt setzen"));
    }
    QSize sizeHint() const override { return QSize(34, 64); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color = palette().color(isEnabled() ? QPalette::ButtonText : QPalette::Mid);
        const double h = height();
        const double xv = width() - 11.0;   // senkrechte Linie der Klammer
        const double yTop = h * 0.25;       // Mitte der oberen Zeile (Position X)
        const double yBottom = h * 0.75;    // Mitte der unteren Zeile (Position Y)
        const double cy = h / 2.0;

        const bool pressed = isDown();
        if (underMouse() || hasFocus() || pressed) {
            QColor hover = palette().color(QPalette::Highlight);
            hover.setAlpha(pressed ? 90 : 40);
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(QRectF(2, cy - 16, width() - 4, 32), 4, 4);
        }

        // Klammer ohne die Stelle des Symbols.
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(1, yTop), QPointF(xv, yTop));
        p.drawLine(QPointF(1, yBottom), QPointF(xv, yBottom));
        p.drawLine(QPointF(xv, yTop), QPointF(xv, cy - 11));
        p.drawLine(QPointF(xv, cy + 11), QPointF(xv, yBottom));

        // Fadenkreuz mit Kreis.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        const QPointF c(xv, cy);
        p.drawEllipse(c, 6.0, 6.0);
        p.drawLine(c + QPointF(-10, 0), c + QPointF(10, 0));
        p.drawLine(c + QPointF(0, -10), c + QPointF(0, 10));
    }
};

}  // namespace ld::app
