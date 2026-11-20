// Zeit für Fristen und Zeitüberschreitungen (Verbindung, "stumm", Reset-Fenster, Schwachlicht-Auto-Aus).
// Sie läuft gleichmäßig (monoton) und springt nicht, wenn jemand die Uhr stellt (NTP, Sommerzeit, Zeitzone):
// die Systemuhr (QDateTime) eignet sich dafür nicht.
#pragma once

#include <QElapsedTimer>
#include <QtGlobal>

namespace ld::grbl {

// Millisekunden seit dem ersten Aufruf im Programm. Immer >= 0.
inline qint64 monotonicMs()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

// Anfangswert für "noch nie passiert": liegt weit genug zurück, dass keine Frist daran hängt.
constexpr qint64 kLongAgoMs = -1'000'000'000;

}  // namespace ld::grbl
