// Simulation (Trockenlauf): Ein Kopf fährt die Laserwege in der Zeit ab, die der Laser brauchen
// würde, mit einem einstellbaren Tempo-Faktor (siehe docs/planung.md 3.11).
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <memory>

#include "core/pathdata.h"

namespace ld::core {

class Simulation : public QObject {
    Q_OBJECT

public:
    enum class State { Stopped, Running, Paused };

    explicit Simulation(QObject* parent = nullptr);

    // Startet von vorn. speed 1 = Echtzeit, 50 = 50-fach schneller.
    void start(std::shared_ptr<const PathData> data, double speed);
    void pause();
    void resume();
    void stop();   // bricht ab (kein finished-Signal)
    void setSpeed(double speed) { speed_ = speed; }

    State state() const { return state_; }
    double elapsedSeconds() const { return elapsed_; }
    double totalSeconds() const { return data_ ? data_->totalSeconds : 0.0; }
    size_t completedSegments() const { return completed_; }

    // Rechnet die Simulation um simSeconds weiter (die Zeitgeber rufen das auf; Tests auch direkt).
    void advanceBy(double simSeconds);

signals:
    // Wegstücke [first, last) sind neu fertig; head ist die aktuelle Position des Kopfes.
    void advanced(size_t first, size_t last, const QPointF& head, double elapsedSeconds);
    void started();
    void finished();   // bis zum Ende gelaufen
    void paused(bool isPaused);

private:
    void tick();

    std::shared_ptr<const PathData> data_;
    QTimer timer_;
    QElapsedTimer clock_;
    State state_ = State::Stopped;
    double speed_ = 50.0;
    double elapsed_ = 0.0;
    size_t completed_ = 0;
};

}  // namespace ld::core
