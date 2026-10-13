#pragma once

#include <QObject>
#include <QTimer>

class GrblDriver;

// Low-power "focus" laser: M3 S<value> followed by G1 F100 without motion.
// In GRBL laser mode ($32=1), M3 keeps the laser on at standstill while the
// modal motion mode is G1. Controllers that still stay dark can be helped by
// switching laser mode off temporarily; the old value is restored when the
// focus laser is switched off, on reset, disconnect and the next connection
// (if the program crashed in between).
class FocusLaser : public QObject
{
    Q_OBJECT
public:
    explicit FocusLaser(GrblDriver* driver, QObject* parent = nullptr);

    bool isOn() const { return m_on; }
    // percent of maxS. Returns false with a reason if not possible.
    bool turnOn(double percent, double maxS, QString* error = nullptr);
    void turnOff();

    void setAutoOffSeconds(int seconds);
    int autoOffSeconds() const { return m_autoOffSeconds; }
    void setDisableLaserMode(bool on) { m_disableLaserMode = on; }

    // Value of $32 that still has to be written back, -1 if none.
    int pendingRestore() const { return m_pendingRestore; }
    void setPendingRestore(int value) { m_pendingRestore = value; }

    int secondsLeft() const;

signals:
    void stateChanged(bool on);
    void pendingRestoreChanged(int value);
    void message(const QString& text);

private:
    void setOn(bool on);
    void restoreLaserMode();
    void setPending(int value);

    GrblDriver* m_driver;
    QTimer m_autoOff;
    int m_autoOffSeconds = 60;
    bool m_on = false;
    bool m_disableLaserMode = false;
    int m_pendingRestore = -1;
};
