#include "ui/tabs/GrblParameterPanel.h"

#include "device/GrblCodes.h"
#include "device/GrblDriver.h"
#include "ui/dialogs/GrblSettingsDialog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColNumber, ColValue, ColUnit, ColDescription };
}

GrblParameterPanel::GrblParameterPanel(GrblDriver* driver, QWidget* parent)
    : QWidget(parent)
    , m_driver(driver)
    , m_table(new QTableWidget(this))
    , m_info(new QLabel(this))
    , m_read(new QPushButton(tr("Read from device"), this))
    , m_write(new QPushButton(tr("Write changed values"), this))
    , m_backup(new QPushButton(tr("Back up to file…"), this))
    , m_restore(new QPushButton(tr("Restore from file…"), this))
{
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("No."), tr("Value"), tr("Unit"), tr("Meaning") });
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(ColDescription, QHeaderView::Stretch);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setMinimumHeight(420);
    m_info->setWordWrap(true);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_read);
    buttons->addWidget(m_write);
    buttons->addStretch(1);
    buttons->addWidget(m_backup);
    buttons->addWidget(m_restore);

    auto* l = new QVBoxLayout(this);
    l->addWidget(m_info);
    l->addWidget(m_table, 1);
    l->addLayout(buttons);

    connect(m_read, &QPushButton::clicked, this, [this] { m_driver->requestSettings(); });
    connect(m_write, &QPushButton::clicked, this, &GrblParameterPanel::writeChanges);
    connect(m_backup, &QPushButton::clicked, this, [this] { backupToFile(m_driver, this); });
    connect(m_restore, &QPushButton::clicked, this, &GrblParameterPanel::restoreFromFile);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_rebuilding || item->column() != ColValue)
            return;
        const bool changed = item->text().trimmed() != item->data(Qt::UserRole).toString();
        QFont f = item->font();
        f.setBold(changed);
        item->setFont(f);
        updateEnabled();
    });
    connect(m_driver, &GrblDriver::settingsReceived, this, &GrblParameterPanel::rebuild);
    connect(m_driver, &GrblDriver::stateChanged, this, &GrblParameterPanel::updateEnabled);
    connect(m_driver, &GrblDriver::disconnected, this, &GrblParameterPanel::rebuild);
    connect(m_driver, &GrblDriver::connectFailed, this, &GrblParameterPanel::rebuild);
    rebuild();
}

void GrblParameterPanel::rebuild()
{
    m_rebuilding = true;
    const GrblSettings& s = m_driver->settings();
    QList<int> numbers = s.values().keys();
    if (numbers.isEmpty())
        numbers = GrblCodes::knownSettings();
    m_table->setRowCount(static_cast<int>(numbers.size()));
    for (int r = 0; r < numbers.size(); ++r) {
        const int n = numbers[r];
        const bool known = GrblCodes::hasSetting(n);
        const GrblCodes::SettingInfo info = GrblCodes::setting(n);

        auto* num = new QTableWidgetItem(QStringLiteral("$%1").arg(n));
        num->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        num->setData(Qt::UserRole, n);
        m_table->setItem(r, ColNumber, num);

        auto* val = new QTableWidgetItem(s.raw(n));
        val->setData(Qt::UserRole, s.raw(n));
        if (!s.has(n))
            val->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(r, ColValue, val);

        auto* unit = new QTableWidgetItem(info.unit);
        unit->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(r, ColUnit, unit);

        const QString desc = known ? QStringLiteral("%1 – %2").arg(info.name, info.description)
                                   : tr("unknown (manufacturer extension)");
        auto* d = new QTableWidgetItem(desc);
        d->setToolTip(desc);
        d->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (!known)
            d->setForeground(QColor(0xf5, 0x7c, 0x00));
        m_table->setItem(r, ColDescription, d);
    }
    m_table->resizeColumnToContents(ColNumber);
    m_table->resizeColumnToContents(ColUnit);
    m_table->setColumnWidth(ColValue, 90);
    m_table->resizeRowsToContents();
    m_rebuilding = false;
    updateEnabled();
}

QList<QPair<int, QString>> GrblParameterPanel::changedValues() const
{
    QList<QPair<int, QString>> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem* v = m_table->item(r, ColValue);
        if (!v)
            continue;
        QString text = v->text().trimmed();
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        if (text != v->data(Qt::UserRole).toString())
            out.append({ m_table->item(r, ColNumber)->data(Qt::UserRole).toInt(), text });
    }
    return out;
}

void GrblParameterPanel::updateEnabled()
{
    const bool connected = m_driver->isConnected();
    const bool idle = connected && !m_driver->isJobRunning() && m_driver->status().state == MachineState::Idle && !m_busy;
    const bool haveSettings = !m_driver->settings().isEmpty();
    m_table->setEnabled(haveSettings);
    m_table->setEditTriggers(idle ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                     | QAbstractItemView::AnyKeyPressed)
                                  : QAbstractItemView::NoEditTriggers);
    m_read->setEnabled(idle);
    m_write->setEnabled(idle && !changedValues().isEmpty());
    m_backup->setEnabled(haveSettings);
    m_restore->setEnabled(idle && haveSettings);
    if (!connected)
        m_info->setText(tr("Connect to the device to see its settings. Explanations of the standard settings are shown below."));
    else if (!idle)
        m_info->setText(tr("Settings can only be changed while the machine is idle."));
    else
        m_info->setText(tr("Double-click a value to change it. Changed values are shown in bold until they are written."));
}

void GrblParameterPanel::writeChanges()
{
    const auto changes = changedValues();
    if (changes.isEmpty())
        return;
    QStringList lines;
    for (const auto& [n, v] : changes)
        lines << QStringLiteral("$%1=%2").arg(n).arg(v);
    if (QMessageBox::question(this, tr("Write settings"),
                              tr("Write these values to the device?\n\n%1").arg(lines.join(QLatin1Char('\n'))))
        != QMessageBox::Yes)
        return;
    m_busy = true;
    updateEnabled();
    m_driver->writeSettings(changes, [this](const QStringList& errors) {
        m_busy = false;
        m_driver->requestSettings();
        if (!errors.isEmpty())
            QMessageBox::warning(this, tr("Write settings"),
                                 tr("Some values were rejected:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        updateEnabled();
    });
}

QString GrblParameterPanel::backupToFile(GrblDriver* driver, QWidget* parent)
{
    if (driver->settings().isEmpty()) {
        QMessageBox::information(parent, tr("Back up settings"), tr("No settings have been read from the device yet."));
        return {};
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggestion = dir + QStringLiteral("/grbl-settings-%1.txt")
                                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
    const QString file = QFileDialog::getSaveFileName(parent, tr("Back up GRBL settings"), suggestion,
                                                      tr("Text files (*.txt);;All files (*)"));
    if (file.isEmpty())
        return {};
    const QString header = QStringLiteral("GRBL settings, saved by Laser-Dilettant %1\nDate: %2\nFirmware: %3\nPort: %4")
                               .arg(QStringLiteral(LD_VERSION),
                                    QDateTime::currentDateTime().toString(Qt::ISODate), driver->bannerLine(),
                                    driver->connectionParams().portName);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, tr("Back up settings"), f.errorString());
        return {};
    }
    f.write(driver->settings().toText(header).toUtf8());
    if (!f.commit()) {
        QMessageBox::warning(parent, tr("Back up settings"), f.errorString());
        return {};
    }
    return file;
}

void GrblParameterPanel::restoreFromFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Restore GRBL settings"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                      tr("Text files (*.txt);;All files (*)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Restore settings"), f.errorString());
        return;
    }
    const GrblSettings backup = GrblSettings::fromText(QString::fromUtf8(f.readAll()));
    if (backup.isEmpty()) {
        QMessageBox::warning(this, tr("Restore settings"), tr("The file contains no $N=value lines."));
        return;
    }
    GrblSettingsDialog dlg(m_driver->settings(), backup, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const auto changes = dlg.selectedChanges();
    if (changes.isEmpty())
        return;
    m_busy = true;
    updateEnabled();
    m_driver->writeSettings(changes, [this](const QStringList& errors) {
        m_busy = false;
        m_driver->requestSettings();
        if (errors.isEmpty())
            QMessageBox::information(this, tr("Restore settings"), tr("The settings were restored."));
        else
            QMessageBox::warning(this, tr("Restore settings"),
                                 tr("Some values were rejected:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        updateEnabled();
    });
}
