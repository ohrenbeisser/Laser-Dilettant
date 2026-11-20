// Schwachlicht: Laser mit sehr wenig Leistung zum Positionieren und Fokussieren (siehe planung.md 3.16, 3.26).
// Mit $32=1 (Laser-Modus) schaltet GRBL den Laser nur während einer Fahrt ein; "M3 S..." im Stand und "G4" bleiben
// dunkel (am echten Laser geprüft, 2026-11-19). Darum fährt das Schwachlicht dauernd +-0,1 mm hin und her
// ("G1 X0.1 S<Wert>" und "G1 X-0.1 S<Wert>" im Wechsel, immer als Paar, damit die Lage gleich bleibt).
// Einschalten: "G91", "M4 S0" und dann die Fahrten; Ausschalten: Vorrat auslaufen lassen, "M5" und "G90".
// Ein Auto-Aus schaltet nach einstellbarer Zeit ab; die Zeit startet bei jedem touch() neu.
// Weil der Kopf dabei fährt, sind Jog und andere Fahrten währenddessen gesperrt (GRBL steht im Zustand Run).
#pragma once

#include <QObject>
#include <QTimer>

namespace ld::grbl {

class GrblConnection;
struct GrblStatus;

class WeakLight : public QObject {
    Q_OBJECT

public:
    explicit WeakLight(GrblConnection* grbl, QObject* parent = nullptr);

    // S-Wert, der 100 % entspricht ($30 bzw. Maximalleistung im Profil).
    void setMaxPowerS(int s) { maxPowerS_ = s; }
    // Obergrenze in Prozent (Einstellung, Vorgabe 5 %) und gewählte Leistung (1 .. Obergrenze).
    void setMaxPercent(int percent);
    int maxPercent() const { return maxPercent_; }
    void setPercent(int percent);
    int percent() const { return percent_; }
    // Auto-Aus nach so vielen Sekunden; 0 = nie.
    void setAutoOffSeconds(int seconds) { autoOffSeconds_ = seconds; }
    int autoOffSeconds() const { return autoOffSeconds_; }

    bool isOn() const { return on_; }
    // Restzeit bis zum Auto-Aus in Sekunden, -1 bei "nie" oder wenn aus.
    int remainingSeconds() const;
    // Verbunden, kein Auftrag, GRBL bereit (oder im Jog)?
    bool canTurnOn() const;

    bool turnOn();
    // Schaltet aus. sendM5 = false, wenn GRBL den Laser ohnehin ausgeschaltet hat (Reset, Alarm, Trennen).
    void turnOff(bool sendM5 = true);
    // Startet die Auto-Aus-Zeit neu (der Bediener arbeitet noch).
    void touch();

signals:
    void changed();                 // an/aus, Leistung oder Restzeit haben sich geändert
    void autoOffTriggered();        // das Auto-Aus hat den Laser ausgeschaltet

private:
    void tick();
    void topUp();   // hält den Vorrat an Fahrten in GRBL gefüllt
    void onStatus(const GrblStatus& status);

    GrblConnection* grbl_;
    int maxPowerS_ = 1000;
    int maxPercent_ = 5;
    int percent_ = 3;
    int autoOffSeconds_ = 30;
    bool on_ = false;
    qint64 deadlineMs_ = 0;
    QTimer timer_;
    QTimer feedTimer_;   // füllt den Vorrat an Fahrten nach
};

}  // namespace ld::grbl
