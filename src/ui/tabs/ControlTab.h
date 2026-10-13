#pragma once

#include "core/DeviceProfile.h"
#include "device/GrblTypes.h"

#include <QWidget>

class ConsoleWidget;
class FocusLaser;
class GrblDriver;
class NumberSpinBox;
class QButtonGroup;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// Tab "Control": jogging, position, focus laser, overrides, G-code file,
// console (section 9.1).
class ControlTab : public QWidget
{
    Q_OBJECT
public:
    ControlTab(GrblDriver* driver, FocusLaser* focus, QWidget* parent = nullptr);

    void setProfile(const DeviceProfile& profile);
    void updateState(bool connected, MachineState state, bool jobRunning);
    ConsoleWidget* console() const { return m_console; }

    // Jog in screen direction (sx, sy each -1, 0, +1); +sy is "up".
    void jogScreen(int sx, int sy);
    // Next/previous step size (Page Up / Page Down).
    void cycleStep(int direction);
    double stepSize() const;

private:
    QWidget* buildJogGroup();
    QWidget* buildPositionGroup();
    QWidget* buildFocusGroup();
    QWidget* buildOverrideGroup();
    QWidget* buildFileGroup();
    void toggleFocus(bool on);
    void updateFocusLabel();

    GrblDriver* m_driver;
    FocusLaser* m_focus;
    DeviceProfile m_profile;
    bool m_connected = false;
    bool m_jobRunning = false;
    MachineState m_state = MachineState::Disconnected;

    QList<QPushButton*> m_jogButtons;
    QButtonGroup* m_stepGroup = nullptr;
    NumberSpinBox* m_jogSpeed = nullptr;
    QPushButton* m_jogStop = nullptr;

    QPushButton* m_setZero = nullptr;
    QPushButton* m_resetZero = nullptr;
    QPushButton* m_home = nullptr;
    QPushButton* m_unlock = nullptr;
    QPushButton* m_reset = nullptr;

    QPushButton* m_focusButton = nullptr;
    NumberSpinBox* m_focusPower = nullptr;
    QLabel* m_focusInfo = nullptr;
    QTimer* m_focusTicker = nullptr;

    QGroupBox* m_overrideGroup = nullptr;
    QSlider* m_feedOv = nullptr;
    QSlider* m_powerOv = nullptr;
    QLabel* m_feedOvLabel = nullptr;
    QLabel* m_powerOvLabel = nullptr;

    ConsoleWidget* m_console = nullptr;
};
