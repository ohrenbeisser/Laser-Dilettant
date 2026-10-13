#include "app/MainWindow.h"

#include "app/AppSettings.h"
#include "app/KeyboardFilter.h"
#include "app/Theme.h"
#include "core/ProfileStore.h"
#include "core/Units.h"
#include "device/CommLog.h"
#include "device/FocusLaser.h"
#include "device/GrblCodes.h"
#include "device/GrblDriver.h"
#include "ui/ConnectionBar.h"
#include "ui/JobBar.h"
#include "ui/StatusLine.h"
#include "ui/WorkArea.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/tabs/ConsoleWidget.h"
#include "ui/tabs/ControlTab.h"
#include "ui/tabs/FirmwarePanel.h"
#include "ui/tabs/PlaceholderTab.h"
#include "ui/tabs/ProfileEditor.h"
#include "ui/tabs/SettingsTab.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(AppSettings* settings, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_profiles(new ProfileStore(QString(), this))
    , m_driver(new GrblDriver(this))
    , m_focus(new FocusLaser(m_driver, this))
    , m_commLog(new CommLog(m_driver, this))
    , m_keyboard(new KeyboardFilter(this))
{
    setWindowTitle(QStringLiteral("Laser-Dilettant"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/laser-dilettant.svg")));
    setMinimumSize(1280, 720);

    m_focus->setPendingRestore(settings->pendingLaserModeRestore());
    m_profiles->load();

    buildUi();
    connectDriver();
    applySettings();
    connect(m_settings, &AppSettings::changed, this, &MainWindow::applySettings);

    refreshProfileList();
    QString profileId = m_settings->lastProfileId();
    if (!m_profiles->find(profileId) && !m_profiles->profiles().isEmpty())
        profileId = m_profiles->profiles().first().id;
    activateProfile(profileId);
    if (m_settings->lastBaud() > 0)
        m_connectionBar->setBaudRate(m_settings->lastBaud());
    m_connectionBar->selectPort(m_settings->lastPort());

    // Keyboard (section 4.6)
    m_keyboard->onEscape = [this] { emergencyStop(); };
    m_keyboard->onSpace = [this] {
        if (!m_driver->isJobRunning())
            return false;
        if (m_driver->status().state == MachineState::Hold)
            m_driver->resume();
        else
            m_driver->pause();
        return true;
    };
    m_keyboard->onArrow = [this](int sx, int sy) { m_controlTab->jogScreen(sx, sy); };
    m_keyboard->onPage = [this](int d) { m_controlTab->cycleStep(d); };
    m_keyboard->jogWidgets = { m_workArea, m_controlTab };
    qApp->installEventFilter(m_keyboard);
    auto* fit = new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this);
    connect(fit, &QShortcut::activated, m_workArea, &WorkArea::fitAll);

    restoreWindowState();
    updateControls();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(m_keyboard);
}

void MainWindow::buildUi()
{
    m_connectionBar = new ConnectionBar(this);
    m_workArea = new WorkArea(this);
    m_jobBar = new JobBar(this);
    m_statusLine = new StatusLine(this);

    m_tabs = new QTabWidget(this);
    m_controlTab = new ControlTab(m_driver, m_focus, m_tabs);
    m_settingsTab = new SettingsTab(m_profiles, m_driver, m_settings, m_tabs);
    m_tabs->addTab(m_controlTab, tr("Control"));
    m_tabs->addTab(new PlaceholderTab(tr("Image"),
                                      tr("Loading and engraving pictures (raster, fill, contour) follows in stages 3, 4 and 6."),
                                      m_tabs),
                   tr("Image"));
    m_tabs->addTab(new PlaceholderTab(tr("Text"), tr("Text with system and single-line fonts follows in stage 5."), m_tabs),
                   tr("Text"));
    m_tabs->addTab(m_settingsTab, tr("Settings"));
    m_tabs->setMinimumWidth(480);

    auto* about = new QToolButton(m_tabs);
    about->setText(QStringLiteral("?"));
    about->setToolTip(tr("About Laser-Dilettant"));
    about->setAutoRaise(true);
    connect(about, &QToolButton::clicked, this, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });
    m_tabs->setCornerWidget(about, Qt::TopRightCorner);

    m_collapseTabs = new QToolButton;
    m_collapseTabs->setCheckable(true);
    m_collapseTabs->setAutoRaise(true);
    m_collapseTabs->setToolTip(tr("Show or hide the tab column"));
    m_workArea->addToolWidget(m_collapseTabs);
    connect(m_collapseTabs, &QToolButton::toggled, this, &MainWindow::setTabsCollapsed);

    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(m_workArea, 1);
    leftLayout->addWidget(m_jobBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(left);
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, true);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        const bool collapsed = m_splitter->sizes().value(1) == 0;
        m_collapseTabs->blockSignals(true);
        m_collapseTabs->setChecked(collapsed);
        m_collapseTabs->setText(collapsed ? tr("Tabs ◀") : tr("Tabs ▶"));
        m_collapseTabs->blockSignals(false);
    });
    m_collapseTabs->setText(tr("Tabs ▶"));

    auto* central = new QWidget(this);
    auto* l = new QVBoxLayout(central);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(m_connectionBar);
    l->addWidget(m_splitter, 1);
    l->addWidget(m_statusLine);
    setCentralWidget(central);

    m_jobBar->setJobControlsAvailable(false);

    connect(m_connectionBar, &ConnectionBar::connectClicked, this, &MainWindow::onConnectClicked);
    connect(m_connectionBar, &ConnectionBar::disconnectClicked, this, &MainWindow::onDisconnectClicked);
    connect(m_connectionBar, &ConnectionBar::emergencyStopClicked, this, &MainWindow::emergencyStop);
    connect(m_connectionBar, &ConnectionBar::profileSelected, this, &MainWindow::activateProfile);
    connect(m_settingsTab->profileEditor(), &ProfileEditor::activateProfile, this, &MainWindow::activateProfile);
    connect(m_profiles, &ProfileStore::changed, this, &MainWindow::refreshProfileList);

    FirmwarePanel* fw = m_settingsTab->firmwarePanel();
    connect(fw, &FirmwarePanel::wizardStarted, this, [this, fw] {
        m_wizardActive = true;
        fw->setContext(m_profile.id, m_connectionBar->params());
    });
    connect(fw, &FirmwarePanel::wizardFinished, this, [this](const QString& profileId) {
        m_wizardActive = false;
        refreshProfileList();
        activateProfile(profileId);
        updateControls();
    });
}

void MainWindow::setTabsCollapsed(bool collapsed)
{
    QList<int> sizes = m_splitter->sizes();
    if (collapsed) {
        if (sizes.value(1) > 0)
            m_splitterSizes = sizes;
        m_splitter->setSizes({ sizes.value(0) + sizes.value(1), 0 });
    } else {
        if (m_splitterSizes.size() == 2 && m_splitterSizes[1] > 0)
            m_splitter->setSizes(m_splitterSizes);
        else
            m_splitter->setSizes({ width() - 560, 560 });
    }
    m_collapseTabs->setText(collapsed ? tr("Tabs ◀") : tr("Tabs ▶"));
}

void MainWindow::connectDriver()
{
    connect(m_driver, &GrblDriver::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_driver, &GrblDriver::statusUpdated, this, [this](const MachineStatus& s) {
        m_statusLine->setStatus(s, m_driver->settings().number(30, m_profile.maxS));
        const bool known = s.state != MachineState::Disconnected && s.state != MachineState::Connecting;
        m_workArea->setLaserPosition(s.mpos.x, s.mpos.y, known);
    });
    connect(m_driver, &GrblDriver::connected, this, &MainWindow::onConnected);
    connect(m_driver, &GrblDriver::connectFailed, this, [this](const QString& msg) {
        updateControls();
        m_statusLine->showMessage(msg, true);
        m_controlTab->console()->appendError(msg);
        if (!m_wizardActive)
            showInfo(tr("Connect"), msg, true);
    });
    connect(m_driver, &GrblDriver::connectionLost, this, [this](const QString& msg) {
        updateControls();
        const QString text = tr("The connection to the laser was lost.\n\n%1\n\n"
                                "The laser may still be on! Switch it off at the device if necessary.")
                                 .arg(msg);
        m_statusLine->showMessage(tr("Connection lost: %1").arg(msg), true);
        m_controlTab->console()->appendError(msg);
        showInfo(tr("Connection lost"), text, true);
    });
    connect(m_driver, &GrblDriver::disconnected, this, [this] {
        updateControls();
        m_statusLine->setFirmware(QString());
        m_statusLine->showMessage(tr("Disconnected."));
    });
    connect(m_driver, &GrblDriver::alarmRaised, this, [this](int code, const QString& text) {
        m_statusLine->showMessage(QStringLiteral("ALARM:%1 – %2").arg(code).arg(text), true);
        // After an emergency stop the alarm is expected; that dialog says enough.
        if (m_lastEmergencyStop.isValid() && m_lastEmergencyStop.elapsed() < 3000)
            return;
        if (!m_wizardActive)
            showAlarmDialog(code, text);
    });
    connect(m_driver, &GrblDriver::errorOccurred, this, [this](const QString& msg) {
        m_statusLine->showMessage(msg, true);
        m_controlTab->console()->appendError(msg);
    });
    connect(m_driver, &GrblDriver::messageReceived, this, [this](const QString& msg) {
        m_statusLine->showMessage(QStringLiteral("GRBL: ") + msg);
    });
    connect(m_focus, &FocusLaser::message, this, [this](const QString& msg) {
        m_statusLine->showMessage(msg);
        m_controlTab->console()->appendInfo(msg);
    });
    connect(m_focus, &FocusLaser::pendingRestoreChanged, m_settings, &AppSettings::setPendingLaserModeRestore);
    connect(m_focus, &FocusLaser::stateChanged, this, &MainWindow::updateControls);
}

void MainWindow::applySettings()
{
    m_driver->setPollInterval(m_settings->pollIntervalMs());
    m_focus->setAutoOffSeconds(m_settings->focusTimeoutS());
    m_focus->setDisableLaserMode(m_settings->focusDisablesLaserMode());
    m_commLog->setEnabled(m_settings->commLogEnabled(), m_settings->commLogDirectory());
    static AppSettings::Theme lastTheme = static_cast<AppSettings::Theme>(-1);
    if (m_settings->theme() != lastTheme) {
        lastTheme = m_settings->theme();
        Theme::apply(lastTheme);
        m_workArea->view()->setProfile(m_profile); // colours follow the palette
    }
}

void MainWindow::restoreWindowState()
{
    QSettings& s = m_settings->raw();
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1440, 860);
    const QList<QVariant> sizes = s.value(QStringLiteral("window/splitter")).toList();
    if (sizes.size() == 2) {
        m_splitter->setSizes({ sizes[0].toInt(), sizes[1].toInt() });
        if (sizes[1].toInt() == 0) {
            m_collapseTabs->blockSignals(true);
            m_collapseTabs->setChecked(true);
            m_collapseTabs->setText(tr("Tabs ◀"));
            m_collapseTabs->blockSignals(false);
        }
    } else {
        m_splitter->setSizes({ 880, 560 });
    }
    m_tabs->setCurrentIndex(qBound(0, s.value(QStringLiteral("window/tab"), 0).toInt(), m_tabs->count() - 1));
}

void MainWindow::saveWindowState()
{
    QSettings& s = m_settings->raw();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    const QList<int> sizes = m_splitter->sizes();
    s.setValue(QStringLiteral("window/splitter"), QVariantList { sizes.value(0), sizes.value(1) });
    s.setValue(QStringLiteral("window/tab"), m_tabs->currentIndex());
}

// --- profiles ------------------------------------------------------------------

void MainWindow::refreshProfileList()
{
    const QString id = m_profile.id.isEmpty() ? m_settings->lastProfileId() : m_profile.id;
    m_connectionBar->setProfiles(m_profiles->profiles(), id);
    if (const DeviceProfile* p = m_profiles->find(m_profile.id)) {
        // The active profile may have been edited.
        m_profile = *p;
        m_workArea->setProfile(m_profile);
        m_controlTab->setProfile(m_profile);
        m_jobBar->setMaxSpeed(m_profile.maxSpeed);
    }
}

void MainWindow::activateProfile(const QString& id)
{
    const DeviceProfile* p = m_profiles->find(id);
    if (!p)
        return;
    m_profile = *p;
    m_settings->setLastProfileId(id);
    m_connectionBar->setProfiles(m_profiles->profiles(), id);
    m_workArea->setProfile(m_profile);
    m_controlTab->setProfile(m_profile);
    m_jobBar->setMaxSpeed(m_profile.maxSpeed);
    m_settingsTab->profileEditor()->setActiveProfile(id);
    m_settingsTab->firmwarePanel()->setContext(id, m_connectionBar->params());
    if (!m_driver->transport())
        m_connectionBar->setBaudRate(m_profile.baudRate);
}

// --- connection ------------------------------------------------------------------

void MainWindow::onConnectClicked()
{
    const ConnectionParams p = m_connectionBar->params();
    if (p.portName.isEmpty()) {
#ifdef Q_OS_WIN
        showInfo(tr("Connect"), tr("No serial port found. Is the laser connected and the CH340 driver installed?"), true);
#else
        showInfo(tr("Connect"), tr("No serial port found. Is the laser connected?"), true);
#endif
        return;
    }
    m_settings->setLastPort(p.portName);
    m_settings->setLastBaud(p.baudRate);
    m_statusLine->showMessage(tr("Connecting to %1 at %2 baud…").arg(p.simulator ? tr("Simulator") : p.portName).arg(p.baudRate));
    m_driver->connectDevice(p);
    updateControls();
}

void MainWindow::onDisconnectClicked()
{
    if (m_driver->isJobRunning()) {
        if (QMessageBox::warning(this, tr("Disconnect"), tr("A job is running. Stop it and disconnect?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }
    m_driver->disconnectDevice();
}

void MainWindow::onConnected()
{
    m_statusLine->setFirmware(m_driver->firmwareVersion());
    m_statusLine->showMessage(tr("Connected: %1").arg(m_driver->bannerLine()));
    m_controlTab->console()->appendInfo(tr("Connected: %1").arg(m_driver->bannerLine()));
    m_settingsTab->firmwarePanel()->setContext(m_profile.id, m_driver->connectionParams());
    updateControls();
    if (!m_wizardActive)
        QTimer::singleShot(300, this, &MainWindow::runPostConnectChecks);
}

void MainWindow::onStateChanged(MachineState state)
{
    m_connectionBar->setMachineState(state);
    if (state == MachineState::Disconnected)
        m_workArea->setLaserPosition(0, 0, false);
    updateControls();
}

void MainWindow::updateControls()
{
    const bool active = m_driver->transport() != nullptr;
    const bool connected = m_driver->isConnected();
    const bool job = m_driver->isJobRunning();
    const MachineState state = m_driver->status().state;
    m_connectionBar->setConnectionActive(active);
    m_connectionBar->setMachineState(active ? state : MachineState::Disconnected);
    m_connectionBar->setProfileChangeAllowed(!job && !m_focus->isOn());
    m_controlTab->updateState(connected, state, job);
    m_settingsTab->profileEditor()->setEditingAllowed(!job);
}

void MainWindow::emergencyStop()
{
    m_lastEmergencyStop.restart();
    if (!m_driver->transport()) {
        m_statusLine->showMessage(tr("Emergency stop: not connected."), true);
        return;
    }
    m_driver->emergencyStop();
    m_controlTab->console()->appendError(tr("EMERGENCY STOP"));
    m_statusLine->showMessage(tr("Emergency stop triggered."), true);
    showInfo(tr("Emergency stop"),
             tr("Emergency stop triggered. Check the position; home the machine or set the origin again if necessary."),
             true);
}

void MainWindow::showInfo(const QString& title, const QString& text, bool warning)
{
    // One non-modal box at a time, so the emergency stop never blocks.
    if (m_infoBox)
        m_infoBox->close();
    auto* box = new QMessageBox(warning ? QMessageBox::Warning : QMessageBox::Information, title, text,
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::NonModal);
    box->show();
    m_infoBox = box;
}

// --- checks after connecting (section 6.3) -----------------------------------------

void MainWindow::runPostConnectChecks()
{
    if (!m_driver->isConnected() || m_closing)
        return;
    if (m_driver->status().state == MachineState::Alarm) {
        showAlarmDialog(-1, QString());
        if (!m_driver->isConnected())
            return;
    }
    checkLaserMode();
    if (m_driver->isConnected())
        checkProfileAgainstDevice();
}

void MainWindow::showAlarmDialog(int code, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, tr("Alarm"), QString(), QMessageBox::NoButton, this);
    if (code >= 0)
        box.setText(tr("ALARM:%1\n\n%2").arg(code).arg(text));
    else
        box.setText(tr("The controller is in alarm state (typical after switching on with homing enabled).\n\n"
                       "Unlock it, or run a homing cycle."));
    QPushButton* unlock = box.addButton(tr("Unlock ($X)"), QMessageBox::AcceptRole);
    QPushButton* home = nullptr;
    if (m_driver->settings().number(22) > 0.5 || m_profile.homing)
        home = box.addButton(tr("Homing ($H)"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == unlock)
        m_driver->unlock();
    else if (home && box.clickedButton() == home)
        m_driver->home();
}

void MainWindow::checkLaserMode()
{
    const GrblSettings& s = m_driver->settings();
    if (m_focus->pendingRestore() >= 0)
        return; // the focus laser restores $32 itself
    if (s.has(32) && s.number(32) > 0.5)
        return;
    const QString text = s.has(32)
        ? tr("Laser mode is off. The laser stops briefly at corners and burns more there. Activate it ($32=1)?")
        : tr("The firmware does not report laser mode ($32). The laser may stop briefly at corners and burn more "
             "there. Try to activate it ($32=1)?");
    if (QMessageBox::question(this, tr("Laser mode"), text) != QMessageBox::Yes)
        return;
    m_driver->writeSettings({ { 32, QStringLiteral("1") } }, [this](const QStringList& errors) {
        if (!errors.isEmpty())
            showInfo(tr("Laser mode"), tr("The device rejected $32=1:\n%1").arg(errors.join(QLatin1Char('\n'))), true);
        else
            m_statusLine->showMessage(tr("Laser mode activated ($32=1)."));
    });
}

void MainWindow::checkProfileAgainstDevice()
{
    const GrblSettings& s = m_driver->settings();
    QStringList diffs;
    DeviceProfile updated = m_profile;
    auto differs = [](double a, double b) { return qAbs(a - b) > 0.01; };
    if (s.has(30) && differs(s.number(30), m_profile.maxS)) {
        diffs << tr("Maximum S value: device $30=%1, profile %2").arg(s.raw(30)).arg(m_profile.maxS);
        updated.maxS = s.number(30);
    }
    if (s.has(130) && s.number(130) > 0 && differs(s.number(130), m_profile.workAreaX)) {
        diffs << tr("Travel X: device $130=%1 mm, profile %2 mm").arg(s.raw(130)).arg(m_profile.workAreaX);
        updated.workAreaX = s.number(130);
    }
    if (s.has(131) && s.number(131) > 0 && differs(s.number(131), m_profile.workAreaY)) {
        diffs << tr("Travel Y: device $131=%1 mm, profile %2 mm").arg(s.raw(131)).arg(m_profile.workAreaY);
        updated.workAreaY = s.number(131);
    }
    if (diffs.isEmpty())
        return;
    QString text = tr("The device reports other values than the profile \"%1\":\n\n%2\n\nTake the device values into the profile?")
                       .arg(m_profile.name, diffs.join(QLatin1Char('\n')));
    if (m_profile.builtin)
        text += QLatin1Char('\n') + tr("(The built-in profile is not changed; a copy is created.)");
    if (QMessageBox::question(this, tr("Device profile"), text) != QMessageBox::Yes)
        return;
    QString error;
    if (m_profile.builtin) {
        const QString id = m_profiles->copy(m_profile.id, tr("%1 (from device)").arg(m_profile.name), &error);
        if (id.isEmpty()) {
            showInfo(tr("Device profile"), error, true);
            return;
        }
        const QString name = m_profiles->find(id)->name;
        updated.id = id;
        updated.name = name;
        updated.builtin = false;
    }
    updated.baudRate = m_driver->connectionParams().baudRate;
    if (!m_profiles->save(updated, &error)) {
        showInfo(tr("Device profile"), error, true);
        return;
    }
    activateProfile(updated.id);
}

// --- closing ------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_driver->isJobRunning()) {
        if (QMessageBox::warning(this, tr("Quit"), tr("A job is running. Stop the laser and quit?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_closing = true;
    saveWindowState();
    if (m_driver->transport()) {
        // Laser off, focus laser mode restored, then close the port.
        m_driver->disconnectDevice();
        QElapsedTimer t;
        t.start();
        while (m_driver->transport() && t.elapsed() < 2500)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    event->accept();
}
