// Jog (Fahren von Hand), siehe docs/planung.md 3.15: Ein Klick oder Tastendruck fährt einen Schritt.
// Wird die Taste gehalten, wiederholt sich der Schritt; beim Loslassen wird der Jog abgebrochen.
// Die Richtung gilt aus Sicht des Bedieners (X nach rechts, Y nach oben/vom Bediener weg); die
// Umrechnung auf die Aufstellung des Lasers steht hier an einer einzigen Stelle.
#pragma once

#include <QObject>
#include <QPointF>
#include <QTimer>

#include "core/machinesetup.h"

namespace ld::grbl {
class GrblConnection;
}

namespace ld::app {

class JogController : public QObject {
    Q_OBJECT

public:
    explicit JogController(ld::grbl::GrblConnection* grbl, QObject* parent = nullptr);

    void setMachine(const ld::core::MachineSetup& machine) { machine_ = machine; }
    void setStepMm(double mm) { stepMm_ = mm; }

    // Darf jetzt gejoggt werden? (verbunden, kein Auftrag, GRBL bereit oder im Jog, kein Alarm)
    bool canJog() const;

    // direction: (1,0) = X+, (-1,0) = X-, (0,1) = Y+ (vom Bediener weg), (0,-1) = Y-.
    void press(const QPointF& direction);
    void release();

private:
    void step();

    ld::grbl::GrblConnection* grbl_;
    ld::core::MachineSetup machine_;
    double stepMm_ = 10.0;
    double speed_ = 3000.0;
    QPointF direction_;
    bool held_ = false;
    bool repeating_ = false;
    QTimer delay_;     // Wartezeit bis zur Wiederholung
    QTimer repeat_;    // Takt der Wiederholung
};

}  // namespace ld::app
