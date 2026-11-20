// Auswahl des Job-Ursprungs: Welcher Punkt des Bildes liegt auf dem Nullpunkt? Fünf Punkte wie die Fünf auf dem
// Würfel (vier Ecken und die Mitte). Von diesem Punkt aus zählen auch die Lineale und die Maße (planung.md 3.23).
#pragma once

#include <QWidget>

#include "core/engraving.h"

namespace ld::app {

class OriginSelector : public QWidget {
    Q_OBJECT

public:
    explicit OriginSelector(QWidget* parent = nullptr);

    ld::core::JobOrigin origin() const { return origin_; }
    void setOrigin(ld::core::JobOrigin origin);   // meldet nichts

signals:
    void originChanged(ld::core::JobOrigin origin);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPointF pointFor(int index) const;
    bool isSelectable(int index) const;   // eine der fünf Wahlmöglichkeiten (Ecken und Mitte)

    ld::core::JobOrigin origin_ = ld::core::JobOrigin::TopLeft;
};

}  // namespace ld::app
