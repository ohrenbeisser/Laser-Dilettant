#include "ui/ConnectionBar.h"

#include "ui/StateColors.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QStyle>
#include <QToolButton>

ConnectionBar::ConnectionBar(QWidget* parent)
    : QWidget(parent)
    , m_port(new QComboBox(this))
    , m_refresh(new QToolButton(this))
    , m_baud(new QComboBox(this))
    , m_connect(new QPushButton(tr("Connect"), this))
    , m_state(new QLabel(this))
    , m_profile(new QComboBox(this))
    , m_estop(new QPushButton(tr("EMERGENCY STOP"), this))
{
    m_port->setMinimumWidth(220);
    m_port->setToolTip(tr("Serial port of the laser"));
    m_refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refresh->setToolTip(tr("Search ports again"));
    for (int b : { 9600, 19200, 38400, 57600, 115200, 230400 })
        m_baud->addItem(QString::number(b), b);
    m_baud->setToolTip(tr("Baud rate"));
    m_connect->setMinimumWidth(110);

    m_state->setAlignment(Qt::AlignCenter);
    m_state->setMinimumWidth(130);
    m_state->setAutoFillBackground(true);
    m_state->setMargin(4);
    QFont bold = m_state->font();
    bold.setBold(true);
    m_state->setFont(bold);

    m_profile->setMinimumWidth(200);
    m_profile->setToolTip(tr("Device profile"));

    m_estop->setToolTip(tr("Stops the laser immediately (soft reset). Shortcut: Esc"));
    m_estop->setMinimumSize(170, 44);
    m_estop->setFocusPolicy(Qt::NoFocus);
    QFont ef = m_estop->font();
    ef.setBold(true);
    ef.setPointSizeF(ef.pointSizeF() * 1.2);
    m_estop->setFont(ef);
    m_estop->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #d50000; color: white; border: 2px solid #8e0000; border-radius: 6px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #ff1744; }"
        "QPushButton:pressed { background-color: #9b0000; }"));

    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(6, 4, 6, 4);
    l->addWidget(new QLabel(tr("Port:"), this));
    l->addWidget(m_port);
    l->addWidget(m_refresh);
    l->addSpacing(8);
    l->addWidget(new QLabel(tr("Baud:"), this));
    l->addWidget(m_baud);
    l->addWidget(m_connect);
    l->addSpacing(8);
    l->addWidget(m_state);
    l->addStretch(1);
    l->addWidget(new QLabel(tr("Profile:"), this));
    l->addWidget(m_profile);
    l->addSpacing(12);
    l->addWidget(m_estop);

    connect(m_refresh, &QToolButton::clicked, this, &ConnectionBar::refreshPorts);
    connect(m_connect, &QPushButton::clicked, this, [this] {
        if (m_active)
            emit disconnectClicked();
        else
            emit connectClicked();
    });
    connect(m_estop, &QPushButton::clicked, this, &ConnectionBar::emergencyStopClicked);
    connect(m_profile, &QComboBox::activated, this, [this](int i) { emit profileSelected(m_profile->itemData(i).toString()); });

    refreshPorts();
    setMachineState(MachineState::Disconnected);
}

void ConnectionBar::refreshPorts()
{
    const QString current = m_port->currentData().toString();
    m_port->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& p : ports) {
        QString label = p.portName();
        if (!p.description().isEmpty())
            label += QStringLiteral(" – ") + p.description();
        m_port->addItem(label, p.systemLocation());
        m_port->setItemData(m_port->count() - 1, p.systemLocation(), Qt::ToolTipRole);
    }
    if (ports.isEmpty()) {
#ifdef Q_OS_WIN
        m_port->setToolTip(tr("No serial port found. Is the laser connected and the CH340 driver installed?"));
#else
        m_port->setToolTip(tr("No serial port found. Is the laser connected?"));
#endif
    } else {
        m_port->setToolTip(tr("Serial port of the laser"));
    }
    m_port->addItem(tr("Simulator"), QString::fromLatin1(kSimulatorPort));
    selectPort(current);
}

void ConnectionBar::selectPort(const QString& port)
{
    const int i = m_port->findData(port);
    if (i >= 0)
        m_port->setCurrentIndex(i);
}

void ConnectionBar::setBaudRate(int baud)
{
    int i = m_baud->findData(baud);
    if (i < 0) {
        m_baud->addItem(QString::number(baud), baud);
        i = m_baud->count() - 1;
    }
    m_baud->setCurrentIndex(i);
}

void ConnectionBar::setProfiles(const QList<DeviceProfile>& profiles, const QString& currentId)
{
    m_profile->clear();
    for (const DeviceProfile& p : profiles)
        m_profile->addItem(p.name, p.id);
    const int i = m_profile->findData(currentId);
    m_profile->setCurrentIndex(i >= 0 ? i : 0);
}

QString ConnectionBar::currentProfileId() const
{
    return m_profile->currentData().toString();
}

ConnectionParams ConnectionBar::params() const
{
    ConnectionParams p;
    p.portName = m_port->currentData().toString();
    p.simulator = p.portName == QLatin1String(kSimulatorPort);
    p.baudRate = m_baud->currentData().toInt();
    return p;
}

void ConnectionBar::setMachineState(MachineState s)
{
    m_state->setText(StateColors::displayName(s));
    QPalette pal = m_state->palette();
    pal.setColor(QPalette::Window, StateColors::background(s));
    pal.setColor(QPalette::WindowText, StateColors::foreground(s));
    m_state->setPalette(pal);
}

void ConnectionBar::setConnectionActive(bool active)
{
    m_active = active;
    m_connect->setText(active ? tr("Disconnect") : tr("Connect"));
    m_port->setEnabled(!active);
    m_refresh->setEnabled(!active);
    m_baud->setEnabled(!active);
}

void ConnectionBar::setProfileChangeAllowed(bool allowed)
{
    m_profile->setEnabled(allowed);
}
