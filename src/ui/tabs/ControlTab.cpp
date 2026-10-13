#include "ui/tabs/ControlTab.h"

#include "core/Units.h"
#include "device/FocusLaser.h"
#include "device/GrblDriver.h"
#include "ui/NumberSpinBox.h"
#include "ui/tabs/ConsoleWidget.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
const double kSteps[] = { 0.1, 1.0, 10.0, 50.0 };
constexpr double kFocusConfirmAbove = 5.0; // percent
}

ControlTab::ControlTab(GrblDriver* driver, FocusLaser* focus, QWidget* parent)
    : QWidget(parent)
    , m_driver(driver)
    , m_focus(focus)
{
    auto* top = new QWidget(this);
    auto* topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(buildJogGroup());
    auto* row = new QHBoxLayout;
    row->addWidget(buildPositionGroup(), 1);
    row->addWidget(buildFocusGroup(), 1);
    topLayout->addLayout(row);
    topLayout->addWidget(buildOverrideGroup());
    topLayout->addWidget(buildFileGroup());
    topLayout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(top);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* consoleBox = new QGroupBox(tr("Console"), this);
    m_console = new ConsoleWidget(driver, consoleBox);
    auto* cl = new QVBoxLayout(consoleBox);
    cl->addWidget(m_console);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(scroll);
    split->addWidget(consoleBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setChildrenCollapsible(false);
    split->setSizes({ 520, 260 });

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->addWidget(split);

    connect(m_focus, &FocusLaser::stateChanged, this, [this](bool on) {
        m_focusButton->blockSignals(true);
        m_focusButton->setChecked(on);
        m_focusButton->blockSignals(false);
        updateFocusLabel();
        updateState(m_connected, m_state, m_jobRunning);
    });
    connect(driver, &GrblDriver::statusUpdated, this, [this](const MachineStatus& s) {
        if (!m_feedOv->isSliderDown()) {
            m_feedOv->blockSignals(true);
            m_feedOv->setValue(s.ovFeed);
            m_feedOv->blockSignals(false);
        }
        if (!m_powerOv->isSliderDown()) {
            m_powerOv->blockSignals(true);
            m_powerOv->setValue(s.ovSpindle);
            m_powerOv->blockSignals(false);
        }
        m_feedOvLabel->setText(QStringLiteral("%1 %").arg(s.ovFeed));
        m_powerOvLabel->setText(QStringLiteral("%1 %").arg(s.ovSpindle));
    });

    updateState(false, MachineState::Disconnected, false);
}

QWidget* ControlTab::buildJogGroup()
{
    auto* box = new QGroupBox(tr("Jog"), this);
    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    struct Dir
    {
        int sx, sy, row, col;
        const char* text;
    };
    // Arrows: screen directions; converted to machine axes per profile.
    const Dir dirs[] = {
        { -1, 1, 0, 0, "↖" }, { 0, 1, 0, 1, "↑" },  { 1, 1, 0, 2, "↗" },
        { -1, 0, 1, 0, "←" }, { 1, 0, 1, 2, "→" },
        { -1, -1, 2, 0, "↙" }, { 0, -1, 2, 1, "↓" }, { 1, -1, 2, 2, "↘" },
    };
    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    for (const Dir& d : dirs) {
        auto* b = new QPushButton(QString::fromUtf8(d.text), box);
        b->setFont(big);
        b->setFixedSize(52, 44);
        b->setAutoRepeat(false);
        const int sx = d.sx, sy = d.sy;
        connect(b, &QPushButton::clicked, this, [this, sx, sy] { jogScreen(sx, sy); });
        grid->addWidget(b, d.row, d.col);
        m_jogButtons << b;
    }
    auto* home = new QPushButton(QStringLiteral("⌂"), box);
    home->setFont(big);
    home->setFixedSize(52, 44);
    home->setToolTip(tr("Move to the work origin (G0 X0 Y0)"));
    connect(home, &QPushButton::clicked, this, [this] { m_driver->sendCommand(QStringLiteral("G90 G0 X0 Y0")); });
    grid->addWidget(home, 1, 1);
    m_jogButtons << home;

    auto* stepBox = new QVBoxLayout;
    stepBox->addWidget(new QLabel(tr("Step:"), box));
    m_stepGroup = new QButtonGroup(this);
    auto* stepRow = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        auto* b = new QPushButton(Units::display(kSteps[i], kSteps[i] < 1 ? 1 : 0) + QStringLiteral(" mm"), box);
        b->setCheckable(true);
        b->setToolTip(tr("Step size (Page Up / Page Down)"));
        m_stepGroup->addButton(b, i);
        stepRow->addWidget(b);
    }
    m_stepGroup->button(2)->setChecked(true);
    stepBox->addLayout(stepRow);

    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(tr("Speed:"), box));
    m_jogSpeed = new NumberSpinBox(0.1, 1000, 1, QStringLiteral("mm/s"), box);
    m_jogSpeed->setValue(50);
    speedRow->addWidget(m_jogSpeed);
    stepBox->addLayout(speedRow);

    m_jogStop = new QPushButton(tr("Stop jogging"), box);
    m_jogStop->setToolTip(tr("Cancels the current jog movement (0x85)"));
    connect(m_jogStop, &QPushButton::clicked, m_driver, &GrblDriver::jogCancel);
    stepBox->addWidget(m_jogStop);
    auto* keyHint = new QLabel(tr("Arrow keys jog when the work area or this tab has the focus."), box);
    keyHint->setWordWrap(true);
    stepBox->addWidget(keyHint);
    stepBox->addStretch(1);

    auto* h = new QHBoxLayout(box);
    h->addLayout(grid);
    h->addSpacing(8);
    h->addLayout(stepBox, 1);
    return box;
}

QWidget* ControlTab::buildPositionGroup()
{
    auto* box = new QGroupBox(tr("Position"), this);
    m_setZero = new QPushButton(tr("Set origin here"), box);
    m_setZero->setToolTip(tr("The current position becomes the work origin (G10 L20 P1 X0 Y0)"));
    m_resetZero = new QPushButton(tr("Reset origin"), box);
    m_resetZero->setToolTip(tr("Work coordinates = machine coordinates (G10 L2 P1 X0 Y0)"));
    m_home = new QPushButton(tr("Homing ($H)"), box);
    m_unlock = new QPushButton(tr("Unlock ($X)"), box);
    m_unlock->setToolTip(tr("Clears an alarm. Check the position afterwards."));
    m_reset = new QPushButton(tr("Reset (Ctrl-X)"), box);
    m_reset->setToolTip(tr("Soft reset of the controller"));

    connect(m_setZero, &QPushButton::clicked, this, [this] { m_driver->sendCommand(QStringLiteral("G10 L20 P1 X0 Y0")); });
    connect(m_resetZero, &QPushButton::clicked, this, [this] { m_driver->sendCommand(QStringLiteral("G10 L2 P1 X0 Y0")); });
    connect(m_home, &QPushButton::clicked, m_driver, &GrblDriver::home);
    connect(m_unlock, &QPushButton::clicked, m_driver, &GrblDriver::unlock);
    connect(m_reset, &QPushButton::clicked, m_driver, &GrblDriver::softReset);

    auto* l = new QVBoxLayout(box);
    for (QPushButton* b : { m_setZero, m_resetZero, m_unlock, m_reset, m_home })
        l->addWidget(b);
    l->addStretch(1);
    return box;
}

QWidget* ControlTab::buildFocusGroup()
{
    auto* box = new QGroupBox(tr("Focus laser"), this);
    m_focusButton = new QPushButton(tr("Focus laser on"), box);
    m_focusButton->setCheckable(true);
    m_focusButton->setToolTip(tr("Low power for focusing and positioning. Wear safety glasses!"));
    m_focusPower = new NumberSpinBox(0.1, 100, 1, QStringLiteral("%"), box);
    m_focusPower->setValue(1.0);
    m_focusInfo = new QLabel(box);
    m_focusInfo->setWordWrap(true);
    m_focusTicker = new QTimer(this);
    m_focusTicker->setInterval(500);
    connect(m_focusTicker, &QTimer::timeout, this, &ControlTab::updateFocusLabel);
    connect(m_focusButton, &QPushButton::toggled, this, &ControlTab::toggleFocus);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Power:"), box));
    row->addWidget(m_focusPower);
    auto* l = new QVBoxLayout(box);
    l->addWidget(m_focusButton);
    l->addLayout(row);
    l->addWidget(m_focusInfo);
    l->addStretch(1);
    updateFocusLabel();
    return box;
}

QWidget* ControlTab::buildOverrideGroup()
{
    m_overrideGroup = new QGroupBox(tr("Overrides (during a job)"), this);
    auto makeRow = [this](const QString& name, QSlider** slider, QLabel** label, auto apply) {
        auto* row = new QHBoxLayout;
        auto* s = new QSlider(Qt::Horizontal, m_overrideGroup);
        s->setRange(10, 200);
        s->setValue(100);
        s->setPageStep(10);
        auto* lab = new QLabel(QStringLiteral("100 %"), m_overrideGroup);
        lab->setMinimumWidth(48);
        auto* reset = new QPushButton(QStringLiteral("100 %"), m_overrideGroup);
        connect(s, &QSlider::sliderReleased, this, [s, apply] { apply(s->value()); });
        connect(s, &QSlider::valueChanged, this, [s, lab, apply](int v) {
            lab->setText(QStringLiteral("%1 %").arg(v));
            if (!s->isSliderDown())
                apply(v);
        });
        connect(reset, &QPushButton::clicked, this, [s, apply] {
            s->setValue(100);
            apply(100);
        });
        row->addWidget(new QLabel(name, m_overrideGroup));
        row->addWidget(s, 1);
        row->addWidget(lab);
        row->addWidget(reset);
        *slider = s;
        *label = lab;
        return row;
    };
    auto* l = new QVBoxLayout(m_overrideGroup);
    l->addLayout(makeRow(tr("Feed:"), &m_feedOv, &m_feedOvLabel, [this](int v) { m_driver->setFeedOverride(v); }));
    l->addLayout(makeRow(tr("Power:"), &m_powerOv, &m_powerOvLabel, [this](int v) { m_driver->setSpindleOverride(v); }));
    l->addStretch(1);
    return m_overrideGroup;
}

QWidget* ControlTab::buildFileGroup()
{
    auto* box = new QGroupBox(tr("G-code file"), this);
    auto* load = new QPushButton(tr("Load G-code…"), box);
    auto* exp = new QPushButton(tr("Export G-code…"), box);
    for (QPushButton* b : { load, exp }) {
        b->setEnabled(false);
        b->setToolTip(tr("Available from stage 2 (NC sender)."));
    }
    auto* l = new QHBoxLayout(box);
    l->addWidget(load);
    l->addWidget(exp);
    l->addStretch(1);
    return box;
}

void ControlTab::setProfile(const DeviceProfile& profile)
{
    m_profile = profile;
    m_jogSpeed->setMaximum(std::max(profile.maxSpeed, profile.jogSpeed));
    m_jogSpeed->setValue(profile.jogSpeed);
    m_focusPower->setValue(profile.focusPower);
    m_home->setVisible(profile.homing);
}

double ControlTab::stepSize() const
{
    const int id = m_stepGroup->checkedId();
    return kSteps[id >= 0 ? id : 2];
}

void ControlTab::cycleStep(int direction)
{
    const int id = std::clamp(m_stepGroup->checkedId() + direction, 0, 3);
    m_stepGroup->button(id)->setChecked(true);
}

void ControlTab::jogScreen(int sx, int sy)
{
    if (!m_connected || m_jobRunning || (m_state != MachineState::Idle && m_state != MachineState::Jog))
        return;
    const double step = stepSize();
    const double dx = sx * step * m_profile.screenSignX();
    const double dy = sy * step * m_profile.screenSignY();
    m_driver->jog(dx, dy, Units::mmPerSecToMmPerMin(m_jogSpeed->value()));
}

void ControlTab::toggleFocus(bool on)
{
    if (!on) {
        m_focus->turnOff();
        return;
    }
    const double power = m_focusPower->value();
    if (power > kFocusConfirmAbove) {
        const auto answer = QMessageBox::warning(
            this, tr("Focus laser"),
            tr("%1 % is a lot for a focus laser and can burn the material or blind you.\n"
               "Really switch on?")
                .arg(Units::display(power, 1)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            m_focusButton->blockSignals(true);
            m_focusButton->setChecked(false);
            m_focusButton->blockSignals(false);
            return;
        }
    }
    QString error;
    if (!m_focus->turnOn(power, m_driver->settings().number(30, m_profile.maxS), &error)) {
        m_focusButton->blockSignals(true);
        m_focusButton->setChecked(false);
        m_focusButton->blockSignals(false);
        QMessageBox::information(this, tr("Focus laser"), error);
    }
}

void ControlTab::updateFocusLabel()
{
    if (m_focus->isOn()) {
        m_focusButton->setText(tr("Focus laser off"));
        m_focusButton->setStyleSheet(QStringLiteral("QPushButton { background-color: #ff6f00; color: white; font-weight: bold; }"));
        m_focusInfo->setText(tr("Laser is ON. Switches off automatically in %1 s.").arg(m_focus->secondsLeft()));
        if (!m_focusTicker->isActive())
            m_focusTicker->start();
    } else {
        m_focusButton->setText(tr("Focus laser on"));
        m_focusButton->setStyleSheet(QString());
        m_focusInfo->setText(tr("Switches off automatically after %1 s.").arg(m_focus->autoOffSeconds()));
        m_focusTicker->stop();
    }
}

void ControlTab::updateState(bool connected, MachineState state, bool jobRunning)
{
    m_connected = connected;
    m_state = state;
    m_jobRunning = jobRunning;
    const bool idle = connected && !jobRunning && state == MachineState::Idle;
    const bool canJog = connected && !jobRunning && (state == MachineState::Idle || state == MachineState::Jog);
    for (QPushButton* b : m_jogButtons)
        b->setEnabled(canJog && !m_focus->isOn());
    m_jogStop->setEnabled(connected && state == MachineState::Jog);
    m_setZero->setEnabled(idle);
    m_resetZero->setEnabled(idle);
    m_home->setEnabled(connected && !jobRunning && (state == MachineState::Idle || state == MachineState::Alarm));
    m_unlock->setEnabled(connected && !jobRunning);
    m_reset->setEnabled(connected);
    m_focusButton->setEnabled(m_focus->isOn() || idle);
    m_overrideGroup->setEnabled(connected && jobRunning);
    m_console->setConnected(connected);
    m_console->setJobRunning(jobRunning);
    updateFocusLabel();
}
