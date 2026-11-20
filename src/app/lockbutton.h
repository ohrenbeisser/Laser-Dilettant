// Schloss-Knopf für "Seitenverhältnis": wird im Bilddialog und im rechten Panel benutzt.
#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace ld::app {

// ---------------------------------------------------------------------------------------------
// Schloss-Knopf: eine Klammer verbindet die Felder Breite und Höhe, in der Mitte sitzt das
// Schloss. Gedrückt (checked) = geschlossen = Seitenverhältnis bleibt erhalten. Das Symbol wird
// selbst gezeichnet und nimmt die Farben der aktuellen Palette (Hell/Dunkel).
// ---------------------------------------------------------------------------------------------
class LockButton : public QToolButton {
public:
    explicit LockButton(QWidget* parent = nullptr) : QToolButton(parent)
    {
        setCheckable(true);
        setChecked(true);  // Standard: geschlossen
        setFixedWidth(34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        updateTip();
        connect(this, &QToolButton::toggled, this, [this] { updateTip(); update(); });
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
        const double yTop = h * 0.25;       // Mitte der oberen Zeile (Breite)
        const double yBottom = h * 0.75;    // Mitte der unteren Zeile (Höhe)
        const double cy = h / 2.0;

        if (underMouse() || hasFocus()) {
            QColor hover = palette().color(QPalette::Highlight);
            hover.setAlpha(40);
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(QRectF(2, cy - 16, width() - 4, 32), 4, 4);
        }

        // Klammer ohne die Stelle des Schlosses.
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(1, yTop), QPointF(xv, yTop));
        p.drawLine(QPointF(1, yBottom), QPointF(xv, yBottom));
        p.drawLine(QPointF(xv, yTop), QPointF(xv, cy - 12));
        p.drawLine(QPointF(xv, cy + 13), QPointF(xv, yBottom));   // vom Schlosskörper (cy + 4 + 9) zur Höhe-Zeile

        // Schloss: Körper und Bügel. Geschlossen sitzt der Bügel mittig auf dem Körper. Offen ist er um
        // seinen linken Fuß nach links weggeschwenkt, das rechte Ende ist frei (wie ein aufgeklapptes Schloss).
        const bool closed = isChecked();
        const double ly = cy + 4.0;   // das Schloss sitzt etwas tiefer, damit der offene Bügel die Klammer nicht berührt
        const QRectF body(xv - 8, ly - 3, 16, 12);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(body, 2, 2);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
        const QPointF pivot(xv - 5.0, ly - 3);   // linker Fuß des Bügels
        QPainterPath shackle;
        shackle.moveTo(pivot);
        shackle.lineTo(xv - 5.0, ly - 5.0);
        shackle.arcTo(QRectF(xv - 5.0, ly - 14.0, 10, 10), 180, -180);
        shackle.lineTo(xv + 5.0, ly - 3);
        p.save();
        if (!closed) {
            p.translate(pivot);
            p.rotate(-38);   // nach links wegdrehen
            p.translate(-pivot);
        }
        p.drawPath(shackle);
        p.restore();
    }

private:
    void updateTip()
    {
        setToolTip(isChecked() ? QObject::tr("Seitenverhältnis bleibt erhalten (zum Lösen klicken)")
                               : QObject::tr("Breite und Höhe sind unabhängig (zum Sperren klicken)"));
    }
};

}  // namespace ld::app
