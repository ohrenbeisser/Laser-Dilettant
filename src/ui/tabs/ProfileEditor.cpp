#include "ui/tabs/ProfileEditor.h"

#include "core/ProfileStore.h"
#include "device/GrblDriver.h"
#include "ui/NumberSpinBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ProfileEditor::ProfileEditor(ProfileStore* store, GrblDriver* driver, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
    , m_driver(driver)
    , m_title(new QLabel(this))
    , m_builtinHint(new QLabel(tr("Built-in profile: it cannot be changed. Make a copy to adjust it."), this))
    , m_baud(new QSpinBox(this))
    , m_areaX(new NumberSpinBox(1, 5000, 1, QStringLiteral("mm"), this))
    , m_areaY(new NumberSpinBox(1, 5000, 1, QStringLiteral("mm"), this))
    , m_origin(new QComboBox(this))
    , m_laserType(new QComboBox(this))
    , m_laserMode(new QComboBox(this))
    , m_maxS(new NumberSpinBox(1, 100000, 0, QString(), this))
    , m_homing(new QCheckBox(tr("Machine has limit switches for homing"), this))
    , m_airAssist(new QCheckBox(tr("Air assist (M8/M9)"), this))
    , m_focusPower(new NumberSpinBox(0.1, 100, 1, QStringLiteral("%"), this))
    , m_maxSpeed(new NumberSpinBox(1, 2000, 0, QStringLiteral("mm/s"), this))
    , m_jogSpeed(new NumberSpinBox(0.1, 2000, 1, QStringLiteral("mm/s"), this))
    , m_save(new QPushButton(tr("Save"), this))
    , m_revert(new QPushButton(tr("Discard changes"), this))
    , m_new(new QPushButton(tr("New…"), this))
    , m_copy(new QPushButton(tr("Copy…"), this))
    , m_rename(new QPushButton(tr("Rename…"), this))
    , m_delete(new QPushButton(tr("Delete"), this))
    , m_fromDevice(new QPushButton(tr("Take values from device"), this))
{
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_builtinHint->setWordWrap(true);
    m_builtinHint->setStyleSheet(QStringLiteral("color: #f57c00;"));

    m_baud->setRange(1200, 2000000);
    m_origin->addItem(tr("Bottom left"), int(Origin::BottomLeft));
    m_origin->addItem(tr("Top left"), int(Origin::TopLeft));
    m_origin->addItem(tr("Top right"), int(Origin::TopRight));
    m_origin->addItem(tr("Bottom right"), int(Origin::BottomRight));
    m_laserType->addItem(tr("Diode laser"), int(LaserType::Diode));
    m_laserType->addItem(tr("CO₂ laser"), int(LaserType::Co2));
    m_laserMode->addItem(tr("M4 – dynamic power (recommended)"), QStringLiteral("M4"));
    m_laserMode->addItem(tr("M3 – constant power"), QStringLiteral("M3"));
    m_maxS->setToolTip(tr("S value for 100 % power, must match $30 of the device"));
    m_fromDevice->setToolTip(tr("Work area from $130/$131, maximum S from $30, homing from $22"));

    auto* form = new QFormLayout;
    form->addRow(tr("Baud rate:"), m_baud);
    form->addRow(tr("Work area X:"), m_areaX);
    form->addRow(tr("Work area Y:"), m_areaY);
    form->addRow(tr("Origin:"), m_origin);
    form->addRow(tr("Laser type:"), m_laserType);
    form->addRow(tr("Laser mode:"), m_laserMode);
    form->addRow(tr("Maximum S ($30):"), m_maxS);
    form->addRow(QString(), m_homing);
    form->addRow(QString(), m_airAssist);
    form->addRow(tr("Focus laser power:"), m_focusPower);
    form->addRow(tr("Maximum speed:"), m_maxSpeed);
    form->addRow(tr("Jog speed:"), m_jogSpeed);

    auto* buttons = new QGridLayout;
    buttons->addWidget(m_save, 0, 0);
    buttons->addWidget(m_revert, 0, 1);
    buttons->addWidget(m_fromDevice, 0, 2);
    buttons->addWidget(m_new, 1, 0);
    buttons->addWidget(m_copy, 1, 1);
    buttons->addWidget(m_rename, 1, 2);
    buttons->addWidget(m_delete, 1, 3);

    auto* l = new QVBoxLayout(this);
    l->addWidget(m_title);
    l->addWidget(m_builtinHint);
    l->addLayout(form);
    l->addLayout(buttons);

    for (NumberSpinBox* s : { m_areaX, m_areaY, m_maxS, m_focusPower, m_maxSpeed, m_jogSpeed })
        connect(s, &NumberSpinBox::valueChanged, this, &ProfileEditor::markDirty);
    connect(m_baud, &QSpinBox::valueChanged, this, &ProfileEditor::markDirty);
    for (QComboBox* c : { m_origin, m_laserType, m_laserMode })
        connect(c, &QComboBox::currentIndexChanged, this, &ProfileEditor::markDirty);
    for (QCheckBox* c : { m_homing, m_airAssist })
        connect(c, &QCheckBox::toggled, this, &ProfileEditor::markDirty);

    connect(m_save, &QPushButton::clicked, this, &ProfileEditor::save);
    connect(m_revert, &QPushButton::clicked, this, [this] { load(m_current); });
    connect(m_new, &QPushButton::clicked, this, &ProfileEditor::newProfile);
    connect(m_copy, &QPushButton::clicked, this, &ProfileEditor::copyProfile);
    connect(m_rename, &QPushButton::clicked, this, &ProfileEditor::renameProfile);
    connect(m_delete, &QPushButton::clicked, this, &ProfileEditor::deleteProfile);
    connect(m_fromDevice, &QPushButton::clicked, this, &ProfileEditor::takeFromDevice);
    connect(m_driver, &GrblDriver::settingsReceived, this, &ProfileEditor::updateEnabled);
    connect(m_driver, &GrblDriver::stateChanged, this, &ProfileEditor::updateEnabled);
}

void ProfileEditor::setActiveProfile(const QString& id)
{
    if (const DeviceProfile* p = m_store->find(id))
        load(*p);
}

void ProfileEditor::setEditingAllowed(bool allowed)
{
    m_editingAllowed = allowed;
    updateEnabled();
}

void ProfileEditor::load(const DeviceProfile& p)
{
    m_loading = true;
    m_current = p;
    m_title->setText(p.name);
    m_baud->setValue(p.baudRate);
    m_areaX->setValue(p.workAreaX);
    m_areaY->setValue(p.workAreaY);
    m_origin->setCurrentIndex(m_origin->findData(int(p.origin)));
    m_laserType->setCurrentIndex(m_laserType->findData(int(p.laserType)));
    m_laserMode->setCurrentIndex(m_laserMode->findData(p.laserMode));
    m_maxS->setValue(p.maxS);
    m_homing->setChecked(p.homing);
    m_airAssist->setChecked(p.airAssist);
    m_maxSpeed->setValue(p.maxSpeed);
    m_jogSpeed->setValue(p.jogSpeed);
    m_focusPower->setValue(p.focusPower);
    m_loading = false;
    m_dirty = false;
    updateEnabled();
}

DeviceProfile ProfileEditor::collect() const
{
    DeviceProfile p = m_current;
    p.baudRate = m_baud->value();
    p.workAreaX = m_areaX->value();
    p.workAreaY = m_areaY->value();
    p.origin = static_cast<Origin>(m_origin->currentData().toInt());
    p.laserType = static_cast<LaserType>(m_laserType->currentData().toInt());
    p.laserMode = m_laserMode->currentData().toString();
    p.maxS = m_maxS->value();
    p.homing = m_homing->isChecked();
    p.airAssist = m_airAssist->isChecked();
    p.focusPower = m_focusPower->value();
    p.maxSpeed = m_maxSpeed->value();
    p.jogSpeed = m_jogSpeed->value();
    return p;
}

void ProfileEditor::markDirty()
{
    if (m_loading)
        return;
    m_dirty = true;
    updateEnabled();
}

void ProfileEditor::updateEnabled()
{
    const bool editable = !m_current.builtin && m_editingAllowed;
    m_builtinHint->setVisible(m_current.builtin);
    for (QWidget* w : std::initializer_list<QWidget*> { m_baud, m_areaX, m_areaY, m_origin, m_laserType, m_laserMode,
                                                        m_maxS, m_homing, m_airAssist, m_focusPower, m_maxSpeed,
                                                        m_jogSpeed })
        w->setEnabled(editable);
    m_save->setEnabled(editable && m_dirty);
    m_revert->setEnabled(editable && m_dirty);
    m_rename->setEnabled(editable);
    m_delete->setEnabled(editable);
    m_new->setEnabled(m_editingAllowed);
    m_copy->setEnabled(m_editingAllowed);
    m_fromDevice->setEnabled(editable && m_driver->isConnected() && !m_driver->settings().isEmpty());
}

void ProfileEditor::save()
{
    QString error;
    const DeviceProfile p = collect();
    if (!m_store->save(p, &error)) {
        QMessageBox::warning(this, tr("Save profile"), error);
        return;
    }
    m_dirty = false;
    emit activateProfile(p.id);
}

void ProfileEditor::newProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New profile"), tr("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QString error;
    const QString id = m_store->create(name.trimmed(), &error);
    if (id.isEmpty())
        QMessageBox::warning(this, tr("New profile"), error);
    else
        emit activateProfile(id);
}

void ProfileEditor::copyProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Copy profile"), tr("Name of the copy:"), QLineEdit::Normal,
                                               tr("%1 (copy)").arg(m_current.name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QString error;
    const QString id = m_store->copy(m_current.id, name.trimmed(), &error);
    if (id.isEmpty())
        QMessageBox::warning(this, tr("Copy profile"), error);
    else
        emit activateProfile(id);
}

void ProfileEditor::renameProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("New name:"), QLineEdit::Normal,
                                               m_current.name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    DeviceProfile p = collect();
    p.name = name.trimmed();
    QString error;
    if (!m_store->save(p, &error))
        QMessageBox::warning(this, tr("Rename profile"), error);
    else
        emit activateProfile(p.id);
}

void ProfileEditor::deleteProfile()
{
    if (QMessageBox::question(this, tr("Delete profile"), tr("Delete the profile \"%1\"?").arg(m_current.name))
        != QMessageBox::Yes)
        return;
    QString error;
    if (!m_store->remove(m_current.id, &error)) {
        QMessageBox::warning(this, tr("Delete profile"), error);
        return;
    }
    emit activateProfile(m_store->profiles().isEmpty() ? QString() : m_store->profiles().first().id);
}

void ProfileEditor::takeFromDevice()
{
    const GrblSettings& s = m_driver->settings();
    m_loading = true;
    if (s.has(130))
        m_areaX->setValue(s.number(130));
    if (s.has(131))
        m_areaY->setValue(s.number(131));
    if (s.has(30))
        m_maxS->setValue(s.number(30));
    if (s.has(22))
        m_homing->setChecked(s.number(22) > 0.5);
    m_baud->setValue(m_driver->connectionParams().baudRate);
    m_loading = false;
    markDirty();
}
