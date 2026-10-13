#include "ui/tabs/ProgramSettingsPanel.h"

#include "app/AppSettings.h"
#include "ui/NumberSpinBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>

#include <algorithm>
ProgramSettingsPanel::ProgramSettingsPanel(AppSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    auto* language = new QComboBox(this);
    language->addItem(tr("System language"), QString());
    language->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->setCurrentIndex(std::max(0, language->findData(settings->language())));

    auto* theme = new QComboBox(this);
    theme->addItem(tr("Like the system"), int(AppSettings::Theme::System));
    theme->addItem(tr("Light"), int(AppSettings::Theme::Light));
    theme->addItem(tr("Dark"), int(AppSettings::Theme::Dark));
    theme->setCurrentIndex(theme->findData(int(settings->theme())));

    auto* confirm = new QCheckBox(tr("Confirm before starting a job"), this);
    confirm->setChecked(settings->confirmBeforeStart());

    auto* afterJob = new QComboBox(this);
    afterJob->addItem(tr("Move to the origin"), int(AppSettings::AfterJob::Origin));
    afterJob->addItem(tr("Move to the start position"), int(AppSettings::AfterJob::StartPosition));
    afterJob->addItem(tr("Stay where it is"), int(AppSettings::AfterJob::Stay));
    afterJob->setCurrentIndex(afterJob->findData(int(settings->afterJob())));

    auto* frameLaser = new QCheckBox(tr("Trace the frame with weak laser light"), this);
    frameLaser->setChecked(settings->frameWithLaser());

    auto* poll = new QSpinBox(this);
    poll->setRange(100, 1000);
    poll->setSingleStep(50);
    poll->setSuffix(QStringLiteral(" ms"));
    poll->setValue(settings->pollIntervalMs());

    auto* focusTimeout = new QSpinBox(this);
    focusTimeout->setRange(5, 600);
    focusTimeout->setSuffix(QStringLiteral(" s"));
    focusTimeout->setValue(settings->focusTimeoutS());

    auto* focusLaserMode = new QCheckBox(tr("Laser mode off ($32=0) during focus"), this);
    focusLaserMode->setToolTip(tr("Switches laser mode off while the focus laser is on. Only needed if the focus "
                                  "laser stays dark. The old value is restored reliably."));
    focusLaserMode->setChecked(settings->focusDisablesLaserMode());

    auto* commLog = new QCheckBox(tr("Communication log file"), this);
    commLog->setToolTip(tr("Writes everything sent to and received from the laser to a text file."));
    commLog->setChecked(settings->commLogEnabled());
    auto* openLogs = new QPushButton(tr("Open folder"), this);
    auto* logRow = new QHBoxLayout;
    logRow->addWidget(commLog);
    logRow->addWidget(openLogs);
    logRow->addStretch(1);

    auto* threshold = new NumberSpinBox(0.5, 100, 1, QStringLiteral("mm"), this);
    threshold->setValue(settings->rapidThresholdMm());
    threshold->setToolTip(tr("White gaps longer than this are skipped with G0 when engraving pictures."));

    auto* form = new QFormLayout(this);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow(tr("Language:"), language);
    form->addRow(tr("Design:"), theme);
    form->addRow(QString(), confirm);
    form->addRow(tr("After the job:"), afterJob);
    form->addRow(QString(), frameLaser);
    form->addRow(tr("Status query interval:"), poll);
    form->addRow(tr("Focus laser switches off after:"), focusTimeout);
    form->addRow(QString(), focusLaserMode);
    form->addRow(QString(), logRow);
    form->addRow(tr("Skip white gaps longer than:"), threshold);

    connect(language, &QComboBox::activated, this, [this, language] {
        m_settings->setLanguage(language->currentData().toString());
        QMessageBox::information(this, tr("Language"), tr("The language changes after restarting the program."));
    });
    connect(theme, &QComboBox::activated, this,
            [this, theme] { m_settings->setTheme(static_cast<AppSettings::Theme>(theme->currentData().toInt())); });
    connect(confirm, &QCheckBox::toggled, settings, &AppSettings::setConfirmBeforeStart);
    connect(afterJob, &QComboBox::activated, this, [this, afterJob] {
        m_settings->setAfterJob(static_cast<AppSettings::AfterJob>(afterJob->currentData().toInt()));
    });
    connect(frameLaser, &QCheckBox::toggled, settings, &AppSettings::setFrameWithLaser);
    connect(poll, &QSpinBox::valueChanged, settings, &AppSettings::setPollIntervalMs);
    connect(focusTimeout, &QSpinBox::valueChanged, settings, &AppSettings::setFocusTimeoutS);
    connect(focusLaserMode, &QCheckBox::toggled, settings, &AppSettings::setFocusDisablesLaserMode);
    connect(commLog, &QCheckBox::toggled, settings, &AppSettings::setCommLogEnabled);
    connect(openLogs, &QPushButton::clicked, this, [this] {
        QDir().mkpath(m_settings->commLogDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings->commLogDirectory()));
    });
    connect(threshold, &NumberSpinBox::valueChanged, settings, &AppSettings::setRapidThresholdMm);
}
