#include "core/simulation.h"

namespace ld::core {

Simulation::Simulation(QObject* parent) : QObject(parent)
{
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &Simulation::tick);
}

void Simulation::start(std::shared_ptr<const PathData> data, double speed)
{
    data_ = std::move(data);
    speed_ = speed;
    elapsed_ = 0.0;
    completed_ = 0;
    if (!data_ || data_->segments.empty()) {
        state_ = State::Stopped;
        timer_.stop();
        return;
    }
    state_ = State::Running;
    clock_.start();
    timer_.start();
    emit started();
}

void Simulation::pause()
{
    if (state_ != State::Running) {
        return;
    }
    state_ = State::Paused;
    timer_.stop();
    emit paused(true);
}

void Simulation::resume()
{
    if (state_ != State::Paused) {
        return;
    }
    state_ = State::Running;
    clock_.restart();
    timer_.start();
    emit paused(false);
}

void Simulation::stop()
{
    timer_.stop();
    state_ = State::Stopped;
}

void Simulation::tick()
{
    const double wallSeconds = static_cast<double>(clock_.restart()) / 1000.0;
    advanceBy(wallSeconds * speed_);
}

void Simulation::advanceBy(double simSeconds)
{
    if (!data_ || state_ == State::Stopped) {
        return;
    }
    elapsed_ += simSeconds;
    const bool done = elapsed_ >= data_->totalSeconds;
    if (done) {
        elapsed_ = data_->totalSeconds;
    }
    const size_t newCompleted = done ? data_->segments.size() : data_->indexAt(elapsed_);
    const size_t first = completed_;
    completed_ = newCompleted;
    emit advanced(first, newCompleted, data_->positionAt(elapsed_), elapsed_);
    if (done) {
        timer_.stop();
        state_ = State::Stopped;
        emit finished();
    }
}

}  // namespace ld::core
