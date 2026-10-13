#include "ui/tabs/FirmwarePanel.h"

#include "app/AppSettings.h"
#include "device/GrblDriver.h"
#include "firmware/AvrdudeRunner.h"
#include "ui/dialogs/FirmwareDialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

FirmwarePanel::FirmwarePanel(GrblDriver* driver, AppSettings* settings, ProfileStore* profiles, QWidget* parent)
    : QWidget(parent)
    , m_driver(driver)
    , m_settings(settings)
    , m_profiles(profiles)
    , m_version(new QLabel(this))
    , m_buildInfo(new QLabel(this))
    , m_avrdudePath(new QLineEdit(this))
    , m_avrdudeStatus(new QLabel(this))
    , m_backup(new QPushButton(tr("Back up firmware…"), this))
    , m_flash(new QPushButton(tr("Flash GRBL 1.1h (assistant)…"), this))
    , m_restore(new QPushButton(tr("Restore firmware backup (assistant)…"), this))
{
    m_buildInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_buildInfo->setWordWrap(true);
    m_avrdudePath->setPlaceholderText(tr("automatic"));
    m_avrdudeStatus->setWordWrap(true);
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_avrdudePath, 1);
    pathRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("Firmware:"), m_version);
    form->addRow(tr("Build info ($I):"), m_buildInfo);
    form->addRow(tr("avrdude:"), pathRow);
    form->addRow(QString(), m_avrdudeStatus);

    auto* hint = new QLabel(tr("The original firmware reports 9600 baud. GRBL 1.1h uses 115200 baud and is faster "
                               "for photos. Back up settings and firmware first – the assistant guides you."),
                            this);
    hint->setWordWrap(true);

    auto* l = new QVBoxLayout(this);
    l->addLayout(form);
    l->addWidget(hint);
    l->addWidget(m_backup);
    l->addWidget(m_flash);
    l->addWidget(m_restore);

    m_avrdudePath->setText(settings->avrdudePath());
    connect(m_avrdudePath, &QLineEdit::editingFinished, this, [this] {
        m_settings->setAvrdudePath(m_avrdudePath->text().trimmed());
        refresh();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("avrdude program"));
        if (!f.isEmpty()) {
            m_avrdudePath->setText(f);
            m_settings->setAvrdudePath(f);
            refresh();
        }
    });
    connect(m_backup, &QPushButton::clicked, this, &FirmwarePanel::backupFirmware);
    connect(m_flash, &QPushButton::clicked, this, [this] { runWizard(false); });
    connect(m_restore, &QPushButton::clicked, this, [this] { runWizard(true); });
    connect(driver, &GrblDriver::buildInfoReceived, this, &FirmwarePanel::refresh);
    connect(driver, &GrblDriver::connected, this, &FirmwarePanel::refresh);
    connect(driver, &GrblDriver::disconnected, this, &FirmwarePanel::refresh);
    refresh();
}

void FirmwarePanel::setContext(const QString& profileId, const ConnectionParams& port)
{
    m_profileId = profileId;
    m_port = port;
}

void FirmwarePanel::refresh()
{
    m_version->setText(m_driver->bannerLine().isEmpty() ? tr("(not connected)") : m_driver->bannerLine());
    m_buildInfo->setText(m_driver->buildInfo().join(QLatin1Char('\n')));
    const QString avr = AvrdudeRunner::findAvrdude(m_settings->avrdudePath());
    if (avr.isEmpty()) {
#ifdef Q_OS_WIN
        m_avrdudeStatus->setText(tr("<span style='color:#c62828'>avrdude not found. It should be next to Laser-Dilettant.exe.</span>"));
#else
        m_avrdudeStatus->setText(tr("<span style='color:#c62828'>avrdude not found. Install it with: sudo apt install avrdude</span>"));
#endif
    } else {
        m_avrdudeStatus->setText(tr("Found: %1").arg(QDir::toNativeSeparators(avr)));
    }
}

bool FirmwarePanel::checkPrerequisites()
{
    if (m_driver->isJobRunning()) {
        QMessageBox::information(this, tr("Firmware"), tr("Not possible while a job is running."));
        return false;
    }
    const ConnectionParams port = m_driver->transport() ? m_driver->connectionParams() : m_port;
    if (port.simulator || port.portName.isEmpty()) {
        QMessageBox::information(this, tr("Firmware"), tr("Please select the serial port of the laser (not the simulator)."));
        return false;
    }
    if (AvrdudeRunner::findAvrdude(m_settings->avrdudePath()).isEmpty()) {
        refresh();
        QMessageBox::warning(this, tr("Firmware"), m_avrdudeStatus->text());
        return false;
    }
    m_port = port;
    return true;
}

void FirmwarePanel::backupFirmware()
{
    if (!checkPrerequisites())
        return;
    const QString suggestion = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/firmware-backup-%1.hex").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
    const QString file = QFileDialog::getSaveFileName(this, tr("Save firmware backup"), suggestion, tr("Intel HEX (*.hex)"));
    if (file.isEmpty())
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Back up firmware"));
    dlg.resize(640, 420);
    auto* baud = FirmwareDialog::createBootBaudCombo(&dlg);
    auto* start = new QPushButton(tr("Start"), &dlg);
    auto* out = new QPlainTextEdit(&dlg);
    out->setReadOnly(true);
    out->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto* status = new QLabel(&dlg);
    status->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto* form = new QFormLayout;
    form->addRow(tr("Boot loader baud rate:"), baud);
    form->addRow(QString(), new QLabel(tr("If it does not work, try the other value."), &dlg));
    auto* l = new QVBoxLayout(&dlg);
    l->addWidget(new QLabel(tr("Port: %1   File: %2").arg(m_port.portName, QDir::toNativeSeparators(file)), &dlg));
    l->addLayout(form);
    l->addWidget(start);
    l->addWidget(out, 1);
    l->addWidget(status);
    l->addWidget(buttons);

    AvrdudeRunner runner;
    runner.setProgram(AvrdudeRunner::findAvrdude(m_settings->avrdudePath()));
    connect(&runner, &AvrdudeRunner::output, out, [out](const QString& t) {
        out->moveCursor(QTextCursor::End);
        out->insertPlainText(t);
    });
    connect(&runner, &AvrdudeRunner::finished, &dlg, [&](bool ok, const QString& msg) {
        QString error;
        if (ok && !AvrdudeRunner::validateHexFile(file, &error)) {
            ok = false;
            status->setText(error);
        } else {
            status->setText(msg);
        }
        status->setStyleSheet(ok ? QStringLiteral("color:#2e7d32") : QStringLiteral("color:#c62828"));
        start->setEnabled(true);
        buttons->setEnabled(true);
    });
    connect(start, &QPushButton::clicked, &dlg, [&] {
        if (m_driver->transport()) {
            m_driver->disconnectDevice();
            QElapsedTimer t;
            t.start();
            while (m_driver->transport() && t.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        out->clear();
        status->clear();
        start->setEnabled(false);
        buttons->setEnabled(false);
        runner.backupFlash(m_port.portName, baud->currentData().toInt(), file);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void FirmwarePanel::runWizard(bool restore)
{
    if (!checkPrerequisites())
        return;
    emit wizardStarted();
    FirmwareDialog dlg(restore ? FirmwareDialog::Mode::RestoreBackup : FirmwareDialog::Mode::FlashGrbl, m_driver,
                       m_settings, m_profiles, m_profileId, m_port, this);
    dlg.exec();
    emit wizardFinished(dlg.resultProfileId());
    refresh();
}
