#include "ui/JobBar.h"

#include "core/Units.h"
#include "ui/NumberSpinBox.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

JobBar::JobBar(QWidget* parent)
    : QWidget(parent)
    , m_material(new QComboBox(this))
    , m_saveMaterial(new QToolButton(this))
    , m_power(new NumberSpinBox(0, 100, 1, QStringLiteral("%"), this))
    , m_speed(new NumberSpinBox(0.1, 100, 1, QStringLiteral("mm/s"), this))
    , m_passes(new QSpinBox(this))
    , m_frame(new QPushButton(tr("Frame"), this))
    , m_start(new QPushButton(tr("Start"), this))
    , m_pause(new QPushButton(tr("Pause"), this))
    , m_stop(new QPushButton(tr("Stop"), this))
    , m_progress(new QProgressBar(this))
    , m_time(new QLabel(this))
{
    m_material->addItem(tr("(no material preset)"));
    m_material->setMinimumWidth(180);
    m_material->setToolTip(tr("Material presets follow in a later stage."));
    m_saveMaterial->setText(QStringLiteral("+"));
    m_saveMaterial->setToolTip(tr("Save current values as material preset"));
    m_saveMaterial->setEnabled(false);

    m_power->setValue(20);
    m_power->setToolTip(tr("Laser power in percent of the maximum ($30)"));
    m_speed->setValue(20);
    m_speed->setToolTip(tr("Engraving speed"));
    m_passes->setRange(1, 99);
    m_passes->setValue(1);
    m_passes->setToolTip(tr("How often the whole job is repeated"));

    m_start->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_pause->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_frame->setToolTip(tr("Trace the bounding box of the motif with the laser off"));

    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%p %"));
    m_progress->setMinimumWidth(160);
    m_time->setText(QStringLiteral("0:00:00 / 0:00:00"));

    auto* row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Material:"), this));
    row1->addWidget(m_material, 1);
    row1->addWidget(m_saveMaterial);
    row1->addSpacing(12);
    row1->addWidget(new QLabel(tr("Power:"), this));
    row1->addWidget(m_power);
    row1->addSpacing(6);
    row1->addWidget(new QLabel(tr("Speed:"), this));
    row1->addWidget(m_speed);
    row1->addSpacing(6);
    row1->addWidget(new QLabel(tr("Passes:"), this));
    row1->addWidget(m_passes);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(m_frame);
    row2->addWidget(m_start);
    row2->addWidget(m_pause);
    row2->addWidget(m_stop);
    row2->addSpacing(12);
    row2->addWidget(m_progress, 1);
    row2->addWidget(m_time);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(6, 4, 6, 4);
    l->setSpacing(4);
    l->addLayout(row1);
    l->addLayout(row2);

    connect(m_power, &NumberSpinBox::valueChanged, this, &JobBar::valuesChanged);
    connect(m_speed, &NumberSpinBox::valueChanged, this, &JobBar::valuesChanged);
    connect(m_passes, &QSpinBox::valueChanged, this, &JobBar::valuesChanged);
    connect(m_frame, &QPushButton::clicked, this, &JobBar::frameClicked);
    connect(m_start, &QPushButton::clicked, this, &JobBar::startClicked);
    connect(m_pause, &QPushButton::clicked, this, &JobBar::pauseClicked);
    connect(m_stop, &QPushButton::clicked, this, &JobBar::stopClicked);

    setJobControlsAvailable(false);
}

double JobBar::powerPercent() const { return m_power->value(); }
double JobBar::speedMmPerSec() const { return m_speed->value(); }
int JobBar::passes() const { return m_passes->value(); }

void JobBar::setMaxSpeed(double mmPerSec)
{
    m_speed->setMaximum(mmPerSec);
}

void JobBar::setJobControlsAvailable(bool available)
{
    const QString later = tr("Available from stage 2 (NC sender).");
    for (QPushButton* b : { m_frame, m_start, m_pause, m_stop }) {
        b->setEnabled(available);
        if (!available)
            b->setToolTip(later);
    }
}

void JobBar::setProgress(qint64 done, qint64 total)
{
    m_progress->setValue(total > 0 ? static_cast<int>(done * 1000 / total) : 0);
}

void JobBar::setTimes(double elapsedS, double totalS)
{
    m_time->setText(Units::duration(elapsedS) + QStringLiteral(" / ") + Units::duration(totalS));
}
