#include "grbl/weaklight.h"

#include <algorithm>
#include <cmath>

#include "grbl/clock.h"
#include "grbl/grblconnection.h"

namespace ld::grbl {

WeakLight::WeakLight(GrblConnection* grbl, QObject* parent) : QObject(parent), grbl_(grbl)
{
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &WeakLight::tick);
    feedTimer_.setInterval(50);
    connect(&feedTimer_, &QTimer::timeout, this, &WeakLight::topUp);

    // Alles, was den Laser ausschaltet oder die Lage ändert, schaltet auch die Anzeige aus.
    connect(grbl_, &GrblConnection::stateChanged, this, [this] {
        if (!grbl_->isConnected()) turnOff(false);
    });
    connect(grbl_, &GrblConnection::alarm, this, [this] { turnOff(false); });
    connect(grbl_, &GrblConnection::resetSent, this, [this] { turnOff(false); });
    connect(grbl_, &GrblConnection::welcome, this, [this] { turnOff(false); });   // Reset: der Laser ist aus
    connect(grbl_, &GrblConnection::jobAborted, this, [this] { turnOff(false); });
    connect(grbl_, &GrblConnection::statusChanged, this, [this](const GrblStatus& status) { onStatus(status); });
}

void WeakLight::setMaxPercent(int percent)
{
    maxPercent_ = std::clamp(percent, 1, 20);
    if (percent_ > maxPercent_) setPercent(maxPercent_);
}

void WeakLight::setPercent(int percent)
{
    percent_ = std::clamp(percent, 1, maxPercent_);
    if (on_) touch();   // die neue Leistung gilt ab der nächsten Fahrt
    emit changed();
}

int WeakLight::remainingSeconds() const
{
    if (!on_ || autoOffSeconds_ <= 0) return -1;
    const qint64 left = deadlineMs_ - monotonicMs();
    return static_cast<int>(std::max<qint64>(0, (left + 999) / 1000));
}

bool WeakLight::canTurnOn() const
{
    if (!grbl_->isConnected() || grbl_->isJobActive()) return false;
    const MachineState state = grbl_->status().state;
    return state == MachineState::Idle || state == MachineState::Jog;
}

void WeakLight::topUp()
{
    if (!on_ || !grbl_->isConnected()) return;
    // GRBL hat 15 Blöcke im Planer; wir halten höchstens 10 Zeilen unterwegs und senden immer Paare,
    // damit der Kopf nach dem Ausschalten wieder genau dort steht, wo er war.
    constexpr int kMaxPending = 10;
    const int s = std::max(1, static_cast<int>(std::lround(percent_ / 100.0 * maxPowerS_)));
    while (grbl_->pendingLines() <= kMaxPending - 2) {
        grbl_->sendLine(QStringLiteral("G1 X0.1 S%1 F300").arg(s));
        grbl_->sendLine(QStringLiteral("G1 X-0.1 S%1").arg(s));
    }
}

bool WeakLight::turnOn()
{
    if (!canTurnOn()) return false;
    grbl_->sendLine(QStringLiteral("G91"));      // relative Maße für die kleinen Fahrten
    grbl_->sendLine(QStringLiteral("M4 S0"));    // Laser-Modus: an nur während der Fahrt
    on_ = true;
    touch();
    topUp();
    feedTimer_.start();
    timer_.start();
    emit changed();
    return true;
}

void WeakLight::turnOff(bool sendM5)
{
    if (!on_) return;
    on_ = false;
    timer_.stop();
    feedTimer_.stop();
    if (sendM5 && grbl_->isConnected() && !grbl_->isJobActive()) {
        // Die schon gesendeten Fahrten laufen noch aus (höchstens eine Drittelsekunde), danach aus und wieder absolut.
        grbl_->sendLine(QStringLiteral("M5"));
        grbl_->sendLine(QStringLiteral("G90"));
    }
    emit changed();
}

void WeakLight::touch()
{
    if (!on_) return;
    deadlineMs_ = monotonicMs() + static_cast<qint64>(autoOffSeconds_) * 1000;
    emit changed();
}

void WeakLight::tick()
{
    if (!on_) return;
    if (autoOffSeconds_ > 0 && monotonicMs() >= deadlineMs_) {
        turnOff(true);
        emit autoOffTriggered();
        return;
    }
    emit changed();   // Restzeit läuft
}

void WeakLight::onStatus(const GrblStatus& status)
{
    // Steht GRBL im Alarm oder in Hold, ist das Schwachlicht nicht mehr gültig. Run ist normal (die Hin-und-her-Fahrt);
    // ein Auftrag von woanders wird über isJobActive() erkannt.
    if (on_ && (status.state == MachineState::Alarm || status.state == MachineState::Hold || grbl_->isJobActive())) {
        turnOff(false);
    }
}

}  // namespace ld::grbl
