#include "jogcontroller.h"

#include "core/gcode.h"
#include "grbl/grblconnection.h"

namespace ld::app {

JogController::JogController(ld::grbl::GrblConnection* grbl, QObject* parent) : QObject(parent), grbl_(grbl)
{
    delay_.setSingleShot(true);
    delay_.setInterval(350);
    repeat_.setInterval(120);
    connect(&delay_, &QTimer::timeout, this, [this] {
        if (!held_) return;
        repeating_ = true;
        step();
        repeat_.start();
    });
    connect(&repeat_, &QTimer::timeout, this, &JogController::step);
}

bool JogController::canJog() const
{
    using ld::grbl::MachineState;
    if (!grbl_->isConnected() || grbl_->isJobActive()) return false;
    const MachineState state = grbl_->status().state;
    return state == MachineState::Idle || state == MachineState::Jog;
}

void JogController::press(const QPointF& direction)
{
    if (held_ || !canJog()) return;
    direction_ = direction;
    held_ = true;
    repeating_ = false;
    step();
    delay_.start();
}

void JogController::release()
{
    if (!held_) return;
    held_ = false;
    delay_.stop();
    repeat_.stop();
    if (repeating_) {
        // Wurde die Taste länger gehalten, soll die Fahrt sofort enden. Ein kurzer Klick fährt seinen Schritt zu Ende.
        grbl_->dropQueuedCommands();
        // Der Jog-Abbruch gilt nur für Fahrten mit "$J="; ein Schritt mit G-Code fährt noch zu Ende (höchstens ein Schritt).
        if (machine_.jogWithJogCommand) grbl_->jogCancel();
    }
    repeating_ = false;
}

void JogController::step()
{
    // Mit G-Code-Jog läuft der Kopf nach dem Schritt noch ("Run"): Bei gehaltener Taste kommt der nächste Schritt
    // erst, wenn der Kopf steht. So fährt er nach dem Loslassen höchstens einen Schritt weiter.
    if (!machine_.jogWithJogCommand && held_ && grbl_->isConnected() && !grbl_->isJobActive() &&
        grbl_->status().state == ld::grbl::MachineState::Run) {
        return;
    }
    if (!canJog()) {
        release();
        return;
    }
    // Schritt aus Sicht des Bedieners in Maschinenkoordinaten umrechnen (Aufstellung).
    const QPointF delta = ld::core::operatorToMachine(machine_, direction_ * stepMm_);
    if (!machine_.jogWithJogCommand) {
        // Normaler G-Code: relativ fahren im Eilgang (der Laser bleibt aus), danach wieder absolut.
        grbl_->sendLine(QStringLiteral("G91 G0 X%1 Y%2")
                            .arg(QString::fromLatin1(ld::core::formatGcodeNumber(delta.x())),
                                 QString::fromLatin1(ld::core::formatGcodeNumber(delta.y()))));
        grbl_->sendLine(QStringLiteral("G90"));
        return;
    }
    const QString line = QStringLiteral("$J=G91 G21 X%1 Y%2 F%3")
                             .arg(QString::fromLatin1(ld::core::formatGcodeNumber(delta.x())),
                                  QString::fromLatin1(ld::core::formatGcodeNumber(delta.y())),
                                  QString::fromLatin1(ld::core::formatGcodeNumber(speed_)));
    grbl_->sendLine(line);
}

}  // namespace ld::app
