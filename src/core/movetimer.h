// Zeitmodell für Fahrten: Länge durch Vorschub, auf Wunsch mit Anfahren und Bremsen.
//
// GRBL fährt gerade, aufeinanderfolgende Strecken in derselben Richtung ohne Halt durch (die
// Zeile eines Rasterbildes besteht aus vielen kurzen G1, ist aber eine einzige Fahrt). Das Modell
// fasst deshalb gleichgerichtete Strecken mit gleichem Vorschub zu einer Fahrt zusammen und
// rechnet pro Fahrt ein Trapez: beschleunigen, mit Vorschub fahren, bremsen. Ist die Strecke zu
// kurz für den vollen Vorschub, ein Dreieck.
#pragma once

#include <QPointF>

namespace ld::core {

class MoveTimer {
public:
    // accelX/accelY in mm/s²; 0 = unbekannt (dann ohne Beschleunigung).
    MoveTimer(double rapidMmPerMin, double accelX, double accelY);

    // feedMmPerMin <= 0 gilt wie Eilgang.
    void add(const QPointF& from, const QPointF& to, bool rapid, double feedMmPerMin);

    // Gesamtzeit in Sekunden (schließt die offene Fahrt ab).
    double seconds();
    // Gesamtzeit nach dem einfachen Modell (Länge durch Vorschub), unabhängig von der Beschleunigung.
    double simpleSeconds() const { return simple_; }

private:
    void flush();

    double rapidMmPerSecond_;
    double accelX_;
    double accelY_;
    double total_ = 0.0;
    double simple_ = 0.0;

    // offene Fahrt
    bool open_ = false;
    double dirX_ = 0.0;
    double dirY_ = 0.0;
    double speed_ = 0.0;     // mm/s
    double length_ = 0.0;
};

}  // namespace ld::core
