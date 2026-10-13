#include "ui/dialogs/FirmwareDialog.h"

#include "app/AppSettings.h"
#include "core/ProfileStore.h"
#include "device/GrblDriver.h"
#include "firmware/AvrdudeRunner.h"
#include "ui/dialogs/GrblSettingsDialog.h"
#include "ui/tabs/GrblParameterPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

QLabel* wrapLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

QPlainTextEdit* outputView(QWidget* parent)
{
    auto* o = new QPlainTextEdit(parent);
    o->setReadOnly(true);
    o->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    o->setMaximumBlockCount(2000);
    return o;
}

QString documentsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

} // namespace

// --- 1. Introduction ---------------------------------------------------------

class FwIntroPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwIntroPage(FirmwareDialog* w)
        : m_w(w)
    {
        setTitle(tr("Change firmware"));
        const bool restore = w->m_mode == FirmwareDialog::Mode::RestoreBackup;
        QString text = restore
            ? tr("<p>This assistant writes a firmware backup (e.g. the original manufacturer firmware) back "
                 "to the controller.</p>")
            : tr("<p>This assistant replaces the firmware of the controller (Arduino Nano / ATmega328P) with "
                 "<b>GRBL 1.1h</b>. Afterwards the laser talks at <b>115200 baud</b>.</p>");
        text += tr("<p><b>The firmware currently on the controller will be overwritten.</b> Without a backup "
                   "the manufacturer firmware cannot be restored.</p>"
                   "<p>Requirements:</p><ul>"
                   "<li>avrdude is installed (Linux: <tt>sudo apt install avrdude</tt>)</li>"
                   "<li>the laser is connected via USB and switched on</li>"
                   "<li>do not unplug the cable while flashing</li></ul>"
                   "<p>Port: <b>%1</b></p>")
                    .arg(w->portName().toHtmlEscaped());
        auto* label = wrapLabel(text, this);
        m_confirm = new QCheckBox(restore ? tr("I understand that the current firmware will be overwritten.")
                                          : tr("I understand that the manufacturer firmware will be overwritten."),
                                  this);
        auto* l = new QVBoxLayout(this);
        l->addWidget(label);
        l->addStretch(1);
        l->addWidget(m_confirm);
        connect(m_confirm, &QCheckBox::toggled, this, &QWizardPage::completeChanged);
    }
    bool isComplete() const override { return m_confirm->isChecked(); }

private:
    FirmwareDialog* m_w;
    QCheckBox* m_confirm;
};

// --- 2. Back up settings ($$) -------------------------------------------------

class FwSettingsPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwSettingsPage(FirmwareDialog* w)
        : m_w(w)
    {
        setTitle(tr("Step 1: Back up the GRBL settings"));
        setSubTitle(tr("Required. The settings ($$) contain steps/mm, speeds, work area and laser mode."));
        m_status = wrapLabel(QString(), this);
        m_save = new QPushButton(tr("Save settings of the connected device…"), this);
        m_choose = new QPushButton(tr("Use an existing backup file…"), this);
        auto* l = new QVBoxLayout(this);
        l->addWidget(m_save);
        l->addWidget(m_choose);
        l->addWidget(m_status);
        l->addStretch(1);
        connect(m_save, &QPushButton::clicked, this, [this] {
            const QString file = GrblParameterPanel::backupToFile(m_w->m_driver, this);
            if (!file.isEmpty()) {
                m_w->m_settingsFile = file;
                m_w->m_savedSettings = m_w->m_driver->settings();
            }
            refresh();
        });
        connect(m_choose, &QPushButton::clicked, this, [this] {
            const QString file = QFileDialog::getOpenFileName(this, tr("GRBL settings backup"), documentsDir(),
                                                              tr("Text files (*.txt);;All files (*)"));
            if (file.isEmpty())
                return;
            QFile f(file);
            GrblSettings s;
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                s = GrblSettings::fromText(QString::fromUtf8(f.readAll()));
            if (s.values().size() < 10) {
                QMessageBox::warning(this, title(), tr("This file does not look like a GRBL settings backup."));
                return;
            }
            m_w->m_settingsFile = file;
            m_w->m_savedSettings = s;
            refresh();
        });
    }
    void initializePage() override { refresh(); }
    bool isComplete() const override { return !m_w->m_settingsFile.isEmpty(); }

private:
    void refresh()
    {
        const bool canRead = m_w->m_driver->isConnected() && !m_w->m_driver->settings().isEmpty();
        m_save->setEnabled(canRead);
        if (!m_w->m_settingsFile.isEmpty())
            m_status->setText(tr("<p style='color:#2e7d32'>Saved: %1 (%2 values)</p>")
                                  .arg(QDir::toNativeSeparators(m_w->m_settingsFile).toHtmlEscaped())
                                  .arg(m_w->m_savedSettings.values().size()));
        else if (!canRead)
            m_status->setText(tr("Not connected: connect to the laser first, or choose an existing backup."));
        else
            m_status->clear();
        emit completeChanged();
    }

    FirmwareDialog* m_w;
    QLabel* m_status;
    QPushButton* m_save;
    QPushButton* m_choose;
};

// --- 3. Back up firmware -------------------------------------------------------

class FwBackupPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwBackupPage(FirmwareDialog* w)
        : m_w(w)
    {
        setTitle(tr("Step 2: Back up the firmware"));
        setSubTitle(tr("Strongly recommended: this is the only way back to the manufacturer firmware."));
        m_bootBaud = FirmwareDialog::createBootBaudCombo(this);
        m_start = new QPushButton(tr("Back up firmware…"), this);
        m_output = outputView(this);
        m_status = wrapLabel(QString(), this);
        m_skip = new QCheckBox(tr("Skip the firmware backup"), this);
        auto* form = new QFormLayout;
        form->addRow(tr("Boot loader baud rate:"), m_bootBaud);
        form->addRow(QString(), wrapLabel(tr("If it does not work, try the other value."), this));
        auto* l = new QVBoxLayout(this);
        l->addLayout(form);
        l->addWidget(m_start);
        l->addWidget(m_output, 1);
        l->addWidget(m_status);
        l->addWidget(m_skip);

        connect(m_bootBaud, &QComboBox::currentIndexChanged, this,
                [this] { m_w->setBootBaud(m_bootBaud->currentData().toInt()); });
        connect(m_start, &QPushButton::clicked, this, &FwBackupPage::start);
        connect(m_skip, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                const auto answer = QMessageBox::warning(
                    this, title(),
                    tr("Without a backup the current firmware is lost for good.\nReally continue without backup?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    m_skip->blockSignals(true);
                    m_skip->setChecked(false);
                    m_skip->blockSignals(false);
                    return;
                }
            }
            m_w->m_firmwareBackupSkipped = on;
            emit completeChanged();
        });
    }
    void initializePage() override
    {
        m_bootBaud->setCurrentIndex(m_bootBaud->findData(m_w->bootBaud()));
        m_skip->setVisible(m_w->m_mode == FirmwareDialog::Mode::FlashGrbl);
    }
    bool isComplete() const override
    {
        if (m_running)
            return false;
        // Restoring a backup does not overwrite anything irreplaceable by default.
        return !m_w->m_firmwareBackupFile.isEmpty() || m_w->m_firmwareBackupSkipped
            || m_w->m_mode == FirmwareDialog::Mode::RestoreBackup;
    }

private:
    void start()
    {
        const QString suggestion = documentsDir()
            + QStringLiteral("/firmware-backup-%1.hex").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
        const QString file = QFileDialog::getSaveFileName(this, tr("Save firmware backup"), suggestion,
                                                          tr("Intel HEX (*.hex)"));
        if (file.isEmpty())
            return;
        m_w->closeConnection();
        m_output->clear();
        m_running = true;
        m_start->setEnabled(false);
        emit completeChanged();
        AvrdudeRunner* r = m_w->avrdude();
        auto out = connect(r, &AvrdudeRunner::output, m_output, [this](const QString& t) {
            m_output->moveCursor(QTextCursor::End);
            m_output->insertPlainText(t);
        });
        auto fin = std::make_shared<QMetaObject::Connection>();
        *fin = connect(r, &AvrdudeRunner::finished, this, [this, out, fin, file](bool ok, const QString& msg) {
            disconnect(out);
            disconnect(*fin);
            m_running = false;
            m_start->setEnabled(true);
            QString error;
            if (ok && !AvrdudeRunner::validateHexFile(file, &error)) {
                ok = false;
                m_status->setText(QStringLiteral("<p style='color:#c62828'>%1</p>").arg(error.toHtmlEscaped()));
            } else {
                m_status->setText(QStringLiteral("<p style='color:%1'>%2</p>")
                                      .arg(ok ? QStringLiteral("#2e7d32") : QStringLiteral("#c62828"), msg.toHtmlEscaped()));
            }
            if (ok)
                m_w->m_firmwareBackupFile = file;
            emit completeChanged();
        });
        r->backupFlash(m_w->portName(), m_w->bootBaud(), file);
    }

    FirmwareDialog* m_w;
    QComboBox* m_bootBaud;
    QPushButton* m_start;
    QPlainTextEdit* m_output;
    QLabel* m_status;
    QCheckBox* m_skip;
    bool m_running = false;
};

// --- 4. Flash ----------------------------------------------------------------------

class FwFlashPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwFlashPage(FirmwareDialog* w)
        : m_w(w)
    {
        const bool restore = w->m_mode == FirmwareDialog::Mode::RestoreBackup;
        setTitle(restore ? tr("Step 3: Write the firmware backup") : tr("Step 3: Flash GRBL"));
        m_bundled = new QRadioButton(tr("GRBL 1.1h (included, 115200 baud)"), this);
        m_custom = new QRadioButton(tr("Other hex file:"), this);
        m_file = new QLineEdit(this);
        auto* browse = new QPushButton(tr("Browse…"), this);
        m_bootBaud = FirmwareDialog::createBootBaudCombo(this);
        m_start = new QPushButton(restore ? tr("Write firmware") : tr("Flash"), this);
        m_output = outputView(this);
        m_status = wrapLabel(QString(), this);

        auto* group = new QButtonGroup(this);
        group->addButton(m_bundled);
        group->addButton(m_custom);
        auto* fileRow = new QHBoxLayout;
        fileRow->addWidget(m_custom);
        fileRow->addWidget(m_file, 1);
        fileRow->addWidget(browse);
        auto* form = new QFormLayout;
        form->addRow(tr("Boot loader baud rate:"), m_bootBaud);

        auto* l = new QVBoxLayout(this);
        l->addWidget(m_bundled);
        l->addLayout(fileRow);
        l->addLayout(form);
        l->addWidget(m_start);
        l->addWidget(m_output, 1);
        l->addWidget(m_status);

        if (restore) {
            m_bundled->hide();
            m_custom->setChecked(true);
        } else {
            m_bundled->setChecked(true);
        }
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Firmware file"), documentsDir(), tr("Intel HEX (*.hex)"));
            if (!f.isEmpty()) {
                m_file->setText(f);
                m_custom->setChecked(true);
            }
        });
        connect(m_bootBaud, &QComboBox::currentIndexChanged, this,
                [this] { m_w->setBootBaud(m_bootBaud->currentData().toInt()); });
        connect(m_start, &QPushButton::clicked, this, &FwFlashPage::start);
    }
    void initializePage() override
    {
        m_bootBaud->setCurrentIndex(m_bootBaud->findData(m_w->bootBaud()));
        if (m_w->m_mode == FirmwareDialog::Mode::RestoreBackup && m_file->text().isEmpty())
            m_file->setText(m_w->m_firmwareBackupFile);
    }
    bool isComplete() const override { return !m_running && m_w->m_flashed; }

private:
    void start()
    {
        const QString file = m_custom->isChecked() ? m_file->text() : m_w->bundledHexPath();
        QString error;
        if (file.isEmpty() || !AvrdudeRunner::validateHexFile(file, &error)) {
            QMessageBox::warning(this, title(), error.isEmpty() ? tr("Please choose a hex file.") : error);
            return;
        }
        if (QMessageBox::question(this, title(),
                                  tr("Write %1 to the controller now?\nDo not unplug the cable until it has finished.")
                                      .arg(QDir::toNativeSeparators(file)))
            != QMessageBox::Yes)
            return;
        m_w->closeConnection();
        m_output->clear();
        m_running = true;
        m_start->setEnabled(false);
        emit completeChanged();
        AvrdudeRunner* r = m_w->avrdude();
        auto out = connect(r, &AvrdudeRunner::output, m_output, [this](const QString& t) {
            m_output->moveCursor(QTextCursor::End);
            m_output->insertPlainText(t);
        });
        auto fin = std::make_shared<QMetaObject::Connection>();
        *fin = connect(r, &AvrdudeRunner::finished, this, [this, out, fin](bool ok, const QString& msg) {
            disconnect(out);
            disconnect(*fin);
            m_running = false;
            m_start->setEnabled(true);
            m_status->setText(QStringLiteral("<p style='color:%1'>%2</p>")
                                  .arg(ok ? QStringLiteral("#2e7d32") : QStringLiteral("#c62828"), msg.toHtmlEscaped()));
            if (ok)
                m_w->m_flashed = true;
            emit completeChanged();
        });
        r->writeFlash(m_w->portName(), m_w->bootBaud(), file);
    }

    FirmwareDialog* m_w;
    QRadioButton* m_bundled;
    QRadioButton* m_custom;
    QLineEdit* m_file;
    QComboBox* m_bootBaud;
    QPushButton* m_start;
    QPlainTextEdit* m_output;
    QLabel* m_status;
    bool m_running = false;
};

// --- 5. Reconnect --------------------------------------------------------------------

class FwReconnectPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwReconnectPage(FirmwareDialog* w)
        : m_w(w)
    {
        setTitle(tr("Step 4: Connect again"));
        m_baud = new QComboBox(this);
        for (int b : { 9600, 19200, 38400, 57600, 115200, 230400 })
            m_baud->addItem(QString::number(b), b);
        m_connect = new QPushButton(tr("Connect"), this);
        m_status = wrapLabel(QString(), this);
        m_saveBaud = new QCheckBox(tr("Store the baud rate in the device profile"), this);
        m_saveBaud->setChecked(true);
        auto* form = new QFormLayout;
        form->addRow(tr("Baud rate:"), m_baud);
        auto* l = new QVBoxLayout(this);
        l->addLayout(form);
        l->addWidget(m_connect);
        l->addWidget(m_status);
        l->addWidget(m_saveBaud);
        l->addStretch(1);

        connect(m_connect, &QPushButton::clicked, this, [this] {
            ConnectionParams p = m_w->m_port;
            p.baudRate = m_baud->currentData().toInt();
            m_status->setText(tr("Connecting…"));
            m_connect->setEnabled(false);
            m_w->m_driver->connectDevice(p);
        });
        connect(w->m_driver, &GrblDriver::connected, this, [this] {
            if (!isVisible())
                return;
            m_connect->setEnabled(true);
            m_status->setText(tr("<p style='color:#2e7d32'>Connected: %1</p>").arg(m_w->m_driver->bannerLine().toHtmlEscaped()));
            emit completeChanged();
        });
        connect(w->m_driver, &GrblDriver::connectFailed, this, [this](const QString& msg) {
            if (!isVisible())
                return;
            m_connect->setEnabled(true);
            m_status->setText(QStringLiteral("<p style='color:#c62828'>%1</p>").arg(msg.toHtmlEscaped()));
        });
    }
    void initializePage() override
    {
        const int baud = m_w->m_mode == FirmwareDialog::Mode::FlashGrbl ? 115200 : 9600;
        m_baud->setCurrentIndex(m_baud->findData(baud));
        m_status->clear();
    }
    bool isComplete() const override { return m_w->m_driver->isConnected(); }
    bool validatePage() override
    {
        if (!m_saveBaud->isChecked())
            return true;
        const DeviceProfile* p = m_w->m_profiles->find(m_w->m_profileId);
        if (!p)
            return true;
        DeviceProfile copy = *p;
        copy.baudRate = m_baud->currentData().toInt();
        if (copy.baudRate == p->baudRate)
            return true;
        QString error;
        if (p->builtin) {
            const QString name = tr("%1 (%2 baud)").arg(p->name).arg(copy.baudRate);
            const QString id = m_w->m_profiles->copy(p->id, name, &error);
            if (id.isEmpty()) {
                QMessageBox::warning(this, title(), error);
                return true;
            }
            copy = *m_w->m_profiles->find(id);
            copy.baudRate = m_baud->currentData().toInt();
        }
        if (!m_w->m_profiles->save(copy, &error))
            QMessageBox::warning(this, title(), error);
        else
            m_w->m_profileId = copy.id;
        return true;
    }

private:
    FirmwareDialog* m_w;
    QComboBox* m_baud;
    QPushButton* m_connect;
    QLabel* m_status;
    QCheckBox* m_saveBaud;
};

// --- 6. Restore settings ---------------------------------------------------------------

class FwRestorePage : public QWizardPage
{
    Q_OBJECT
public:
    explicit FwRestorePage(FirmwareDialog* w)
        : m_w(w)
    {
        setTitle(tr("Step 5: Restore the settings"));
        setSubTitle(tr("The new firmware may start with default values. Compare and write the saved settings."));
        m_compare = new QPushButton(tr("Compare and restore…"), this);
        m_status = wrapLabel(QString(), this);
        auto* l = new QVBoxLayout(this);
        l->addWidget(m_compare);
        l->addWidget(m_status);
        l->addStretch(1);
        connect(m_compare, &QPushButton::clicked, this, &FwRestorePage::compare);
    }
    void initializePage() override { m_status->clear(); }

private:
    void compare()
    {
        GrblDriver* d = m_w->m_driver;
        if (!d->isConnected()) {
            QMessageBox::warning(this, title(), tr("Not connected."));
            return;
        }
        GrblSettingsDialog dlg(d->settings(), m_w->m_savedSettings, this);
        if (dlg.exec() != QDialog::Accepted) {
            checkLaserMode();
            return;
        }
        m_compare->setEnabled(false);
        d->writeSettings(dlg.selectedChanges(), [this, d](const QStringList& errors) {
            m_compare->setEnabled(true);
            d->requestSettings([this](bool, const QString&) { checkLaserMode(); });
            if (!errors.isEmpty())
                m_status->setText(QStringLiteral("<p style='color:#c62828'>%1</p>")
                                      .arg(errors.join(QStringLiteral("<br>")).toHtmlEscaped()));
            else
                m_status->setText(tr("<p style='color:#2e7d32'>The settings were restored.</p>"));
        });
    }

    void checkLaserMode()
    {
        GrblDriver* d = m_w->m_driver;
        if (d->settings().number(32, 0) > 0.5)
            return;
        if (QMessageBox::question(this, title(),
                                  tr("Laser mode ($32) is off. Without it the laser stops briefly at corners and "
                                     "burns more there.\nSwitch laser mode on ($32=1)?"))
            == QMessageBox::Yes)
            d->writeSettings({ { 32, QStringLiteral("1") } }, [d](const QStringList&) { d->requestSettings(); });
    }

    FirmwareDialog* m_w;
    QPushButton* m_compare;
    QLabel* m_status;
};

// --- dialog ---------------------------------------------------------------------------

FirmwareDialog::FirmwareDialog(Mode mode, GrblDriver* driver, AppSettings* settings, ProfileStore* profiles,
                               const QString& profileId, const ConnectionParams& port, QWidget* parent)
    : QWizard(parent)
    , m_mode(mode)
    , m_driver(driver)
    , m_settings(settings)
    , m_profiles(profiles)
    , m_profileId(profileId)
    , m_port(port)
    , m_avrdude(new AvrdudeRunner(this))
{
    setWindowTitle(mode == Mode::FlashGrbl ? tr("Flash GRBL") : tr("Restore firmware"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnLastPage);
    setOption(QWizard::NoCancelButtonOnLastPage);
    resize(720, 580);
    m_avrdude->setProgram(AvrdudeRunner::findAvrdude(settings->avrdudePath()));

    addPage(new FwIntroPage(this));
    addPage(new FwSettingsPage(this));
    addPage(new FwBackupPage(this));
    addPage(new FwFlashPage(this));
    addPage(new FwReconnectPage(this));
    addPage(new FwRestorePage(this));
}

FirmwareDialog::~FirmwareDialog()
{
    if (!m_tempHex.isEmpty())
        QFile::remove(m_tempHex);
}

QComboBox* FirmwareDialog::createBootBaudCombo(QWidget* parent)
{
    auto* c = new QComboBox(parent);
    c->addItem(tr("57600 (old Nano boot loader)"), 57600);
    c->addItem(tr("115200 (new boot loader, Uno)"), 115200);
    return c;
}

void FirmwareDialog::closeConnection()
{
    // avrdude needs the port for itself.
    if (m_driver->transport())
        m_driver->disconnectDevice();
    QElapsedTimer t;
    t.start();
    while (m_driver->transport() && t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

QString FirmwareDialog::bundledHexPath()
{
    if (m_tempHex.isEmpty() || !QFile::exists(m_tempHex)) {
        m_tempHex = QDir::temp().filePath(QStringLiteral("laser-dilettant-grbl_v1.1h-%1.hex").arg(QCoreApplication::applicationPid()));
        QFile::remove(m_tempHex);
        QFile::copy(QStringLiteral(":/firmware/grbl_v1.1h.hex"), m_tempHex);
        QFile::setPermissions(m_tempHex, QFile::ReadOwner | QFile::WriteOwner);
    }
    return m_tempHex;
}

void FirmwareDialog::reject()
{
    if (m_avrdude->isRunning()) {
        const auto answer = QMessageBox::warning(this, windowTitle(),
                                                 tr("avrdude is still running. Cancelling while flashing can leave "
                                                    "the controller without working firmware.\nCancel anyway?"),
                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        m_avrdude->cancel();
    }
    QWizard::reject();
}

#include "FirmwareDialog.moc"
