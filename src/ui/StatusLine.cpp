#include "ui/StatusLine.h"

#include "core/Units.h"
#include "ui/StateColors.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

namespace {
QLabel* field(QWidget* parent, int minWidth)
{
    auto* l = new QLabel(parent);
    l->setMinimumWidth(minWidth);
    l->setFrameShape(QFrame::StyledPanel);
    l->setFrameShadow(QFrame::Sunken);
    l->setMargin(1);
    return l;
}
} // namespace

StatusLine::StatusLine(QWidget* parent)
    : QWidget(parent)
    , m_state(field(this, 110))
    , m_x(field(this, 95))
    , m_y(field(this, 95))
    , m_feed(field(this, 80))
    , m_power(field(this, 70))
    , m_ov(field(this, 100))
    , m_firmware(field(this, 90))
    , m_message(new QLabel(this))
{
    m_ov->setToolTip(tr("Overrides: feed / power"));
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(6, 2, 6, 2);
    l->addWidget(m_state);
    l->addWidget(m_x);
    l->addWidget(m_y);
    l->addWidget(m_feed);
    l->addWidget(m_power);
    l->addWidget(m_ov);
    l->addWidget(m_firmware);
    l->addWidget(m_message, 1);
    MachineStatus s;
    setStatus(s, 1000);
    setFirmware(QString());
}

void StatusLine::setStatus(const MachineStatus& s, double maxS)
{
    m_state->setText(StateColors::displayName(s.state));
    const bool known = s.state != MachineState::Disconnected && s.state != MachineState::Connecting;
    m_x->setText(QStringLiteral("X %1").arg(known ? Units::display(s.wpos.x, 2) : QStringLiteral("–")));
    m_y->setText(QStringLiteral("Y %1").arg(known ? Units::display(s.wpos.y, 2) : QStringLiteral("–")));
    m_x->setToolTip(tr("Work position X; machine position %1 mm").arg(Units::display(s.mpos.x, 2)));
    m_y->setToolTip(tr("Work position Y; machine position %1 mm").arg(Units::display(s.mpos.y, 2)));
    m_feed->setText(QStringLiteral("F %1").arg(known ? QString::number(qRound(s.feed)) : QStringLiteral("–")));
    m_feed->setToolTip(tr("Current feed in mm/min (%1 mm/s)").arg(Units::display(Units::mmPerMinToMmPerSec(s.feed), 1)));
    m_power->setText(QStringLiteral("S %1 %").arg(known ? Units::display(Units::sToPercent(s.spindle, maxS), 0) : QStringLiteral("–")));
    m_ov->setText(QStringLiteral("Ov %1/%2").arg(s.ovFeed).arg(s.ovSpindle));
}

void StatusLine::setFirmware(const QString& version)
{
    m_firmware->setText(version.isEmpty() ? QStringLiteral("Grbl –") : QStringLiteral("Grbl ") + version);
}

void StatusLine::showMessage(const QString& text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? QStringLiteral("color: #e53935; font-weight: bold;") : QString());
    const int serial = ++m_messageSerial;
    if (!error) {
        QTimer::singleShot(8000, this, [this, serial] {
            if (serial == m_messageSerial)
                m_message->clear();
        });
    }
}
