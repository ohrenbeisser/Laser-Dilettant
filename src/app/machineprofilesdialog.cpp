#include "machineprofilesdialog.h"
#include "helpwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "appsettings.h"
#include "machineprofiles.h"
#include "unitspinboxes.h"

namespace ld::app {

using ld::core::LaserCommand;
using ld::core::MachineSetup;

MachineProfilesDialog::MachineProfilesDialog(MachineProfiles* profiles, QWidget* parent)
    : QDialog(parent), profiles_(profiles)
{
    setWindowTitle(tr("Maschinenprofile"));
    attachHelp(this, ld::core::helptopic::kMachineProfile);   // F1 und Knopf "Hilfe" (Handbuch)
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(820, 560);

    activeBefore_ = profiles_->activeName();
    for (const QString& name : profiles_->names()) {
        if (const auto setup = profiles_->load(name)) {
            entries_.push_back({name, *setup});
        }
    }
    if (entries_.empty()) {
        entries_.push_back({QString(), MachineSetup{}});   // es gibt immer mindestens ein Profil
    }
    buildUi();
    int select = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (QString::compare(entries_[i].originalName, activeBefore_, Qt::CaseInsensitive) == 0) {
            select = static_cast<int>(i);
        }
    }
    refreshList(select);
}

void MachineProfilesDialog::buildUi()
{
    auto* main = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    main->addLayout(body, 1);

    // ------------------------------------------------------------ links: Liste und Knöpfe
    auto* left = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setMinimumWidth(190);
    left->addWidget(list_, 1);
    auto* newButton = new QPushButton(tr("Neu"));
    auto* copyButton = new QPushButton(tr("Kopieren"));
    auto* renameButton = new QPushButton(tr("Umbenennen …"));
    auto* deleteButton = new QPushButton(tr("Löschen"));
    auto* row1 = new QHBoxLayout;
    row1->addWidget(newButton);
    row1->addWidget(copyButton);
    auto* row2 = new QHBoxLayout;
    row2->addWidget(renameButton);
    row2->addWidget(deleteButton);
    left->addLayout(row1);
    left->addLayout(row2);
    body->addLayout(left);

    // ------------------------------------------------------------ rechts: Reiter
    auto* right = new QVBoxLayout;
    nameLabel_ = new QLabel;
    QFont bold = nameLabel_->font();
    bold.setBold(true);
    nameLabel_->setFont(bold);
    right->addWidget(nameLabel_);
    auto* tabs = new QTabWidget;
    right->addWidget(tabs, 1);
    body->addLayout(right, 1);

    // Reiter "Maschine"
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        workAreaX_ = new LengthSpinBox(1.0, 5000.0, 1, 3, 10.0);
        workAreaY_ = new LengthSpinBox(1.0, 5000.0, 1, 3, 10.0);
        workAreaX_->setToolTip(tr("Verfahrweg der X-Achse der Maschine"));
        workAreaY_->setToolTip(tr("Verfahrweg der Y-Achse der Maschine"));
        form->addRow(tr("Arbeitsbereich X"), workAreaX_);
        form->addRow(tr("Arbeitsbereich Y"), workAreaY_);
        rotation_ = new QComboBox;
        for (const int degrees : {0, 90, 180, 270}) {
            rotation_->addItem(tr("%1°").arg(degrees), degrees);
        }
        rotation_->setToolTip(tr("Drehung des ganzen Lasers im Uhrzeigersinn, von oben gesehen. "
                                 "0° heißt: X zeigt nach rechts, Y vom Bediener weg."));
        form->addRow(tr("Aufstellung: Drehung"), rotation_);
        mirror_ = new QCheckBox(tr("X-Achse der Maschine gespiegelt"));
        form->addRow(QString(), mirror_);
        dpi_ = new QDoubleSpinBox;
        dpi_->setRange(10.0, 10000.0);
        dpi_->setDecimals(0);
        dpi_->setSuffix(tr(" dpi"));
        dpi_->setKeyboardTracking(false);
        form->addRow(tr("Laserauflösung"), dpi_);
        pixelSizeLabel_ = new QLabel;
        pixelSizeLabel_->setEnabled(false);
        form->addRow(QString(), pixelSizeLabel_);
        hasZ_ = new QCheckBox(tr("Hat eine Z-Achse"));
        hasEndstops_ = new QCheckBox(tr("Hat Endschalter"));
        hasEndstops_->setToolTip(tr("Wird gespeichert; die Wirkung kommt in einer späteren Version."));
        form->addRow(QString(), hasZ_);
        form->addRow(QString(), hasEndstops_);
        tabs->addTab(page, tr("Maschine"));
    }
    // Reiter "Laser"
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        command_ = new QComboBox;
        command_->addItem(tr("M4: Laser-Modus (dynamische Leistung, $32=1)"), static_cast<int>(LaserCommand::M4));
        command_->addItem(tr("M3: konstante Leistung"), static_cast<int>(LaserCommand::M3));
        command_->setToolTip(tr("Welcher Befehl den Laser einschaltet. Mit $32=0 in GRBL ist M3 nötig."));
        form->addRow(tr("Laser-Befehl"), command_);
        maxPower_ = new QSpinBox;
        maxPower_->setRange(1, 100000);
        maxPower_->setToolTip(tr("S-Wert für 100 % Leistung (der Wert von $30 in GRBL). Die App sendet nie mehr."));
        form->addRow(tr("Maximale Leistung S"), maxPower_);
        rapid_ = new SpeedSpinBox(100.0, 100000.0);
        rapid_->setToolTip(tr("Eilgang der Maschine, für die Zeitschätzung"));
        form->addRow(tr("Eilgang"), rapid_);
        maxSpeed_ = new SpeedSpinBox(100.0, 100000.0);
        maxSpeed_->setToolTip(tr("Höchste Speed beim Gravieren und Schneiden. Der Speed-Regler im rechten Panel geht nicht darüber."));
        form->addRow(tr("Maximale Speed"), maxSpeed_);
        accelX_ = new QDoubleSpinBox;
        accelY_ = new QDoubleSpinBox;
        for (QDoubleSpinBox* spin : {accelX_, accelY_}) {
            spin->setRange(0.0, 1000000.0);
            spin->setDecimals(0);
            spin->setSuffix(tr(" mm/s²"));
            spin->setSpecialValueText(tr("unbekannt"));
            spin->setKeyboardTracking(false);
            spin->setToolTip(tr("Beschleunigung der Achse (GRBL $120/$121). Mit Wert rechnet die Zeitschätzung "
                                "realistischer und das Panel zeigt einen Überscan-Richtwert."));
        }
        form->addRow(tr("Beschleunigung X"), accelX_);
        form->addRow(tr("Beschleunigung Y"), accelY_);
        correctionX_ = new QDoubleSpinBox;
        correctionY_ = new QDoubleSpinBox;
        for (QDoubleSpinBox* spin : {correctionX_, correctionY_}) {
            spin->setRange(0.5, 2.0);
            spin->setDecimals(4);
            spin->setSingleStep(0.001);
            spin->setKeyboardTracking(false);
            spin->setToolTip(tr("Korrekturfaktor der Achse aus der Kalibrierung (Menü Maschine). Die Koordinaten im G-Code "
                                "werden damit multipliziert. 1,0000 = keine Korrektur."));
        }
        form->addRow(tr("Korrektur X"), correctionX_);
        form->addRow(tr("Korrektur Y"), correctionY_);
        tabs->addTab(page, tr("Laser"));
    }
    // Reiter "G-Code"
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(tr("Start-G-Code (steht am Anfang der Datei, vor dem Einschalten des Lasers):")));
        start_ = new QPlainTextEdit;
        start_->setFixedHeight(90);
        layout->addWidget(start_);
        layout->addWidget(new QLabel(tr("Ende-G-Code (der Laser wird am Ende immer mit M5 ausgeschaltet, "
                                        "auch wenn es hier fehlt):")));
        end_ = new QPlainTextEdit;
        end_->setFixedHeight(90);
        layout->addWidget(end_);
        moveToZeroBefore_ = new QCheckBox(tr("Vor dem Start zum Nullpunkt fahren"));
        moveToZeroBefore_->setToolTip(tr("Nicht angehakt: Der Auftrag geht davon aus, dass der Laser schon auf dem Nullpunkt steht."));
        layout->addWidget(moveToZeroBefore_);
        returnToZeroAfter_ = new QCheckBox(tr("Nach dem Auftrag zum Nullpunkt zurückfahren"));
        returnToZeroAfter_->setToolTip(tr("Nicht angehakt: Der Laser bleibt dort stehen, wo der Auftrag endet."));
        layout->addWidget(returnToZeroAfter_);
        jogWithJogCommand_ = new QCheckBox(tr("Jog mit dem GRBL-Befehl $J= (Standard von GRBL)"));
        jogWithJogCommand_->setToolTip(tr("Nicht angehakt: Das Fahren von Hand benutzt normalen G-Code (G91, G0, G90). "
                                          "Das geht auf jeder GRBL-Variante. Manche Varianten (zum Beispiel 1.1z) bestätigen $J= mit \"ok\", "
                                          "fahren aber nicht. Angehakt: Der Jog-Befehl von GRBL wird benutzt; er hält beim Loslassen der Taste sofort an."));
        layout->addWidget(jogWithJogCommand_);
        layout->addStretch();
        tabs->addTab(page, tr("G-Code"));
    }

    // ------------------------------------------------------------ unten: OK / Abbrechen
    auto* buttons = new QDialogButtonBox;
    buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    main->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(list_, &QListWidget::currentRowChanged, this, &MachineProfilesDialog::showEntry);
    connect(newButton, &QPushButton::clicked, this, &MachineProfilesDialog::newProfile);
    connect(copyButton, &QPushButton::clicked, this, &MachineProfilesDialog::copyProfile);
    connect(renameButton, &QPushButton::clicked, this, &MachineProfilesDialog::renameProfile);
    connect(deleteButton, &QPushButton::clicked, this, &MachineProfilesDialog::deleteProfile);

    // Jede Änderung im Formular geht sofort in das Profil der Arbeitskopie.
    auto changed = [this] { readForm(); };
    connect(workAreaX_, &LengthSpinBox::mmChanged, this, changed);
    connect(workAreaY_, &LengthSpinBox::mmChanged, this, changed);
    connect(rotation_, &QComboBox::currentIndexChanged, this, changed);
    connect(mirror_, &QCheckBox::toggled, this, changed);
    connect(hasZ_, &QCheckBox::toggled, this, changed);
    connect(hasEndstops_, &QCheckBox::toggled, this, changed);
    connect(dpi_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(command_, &QComboBox::currentIndexChanged, this, changed);
    connect(maxPower_, &QSpinBox::valueChanged, this, changed);
    connect(rapid_, &SpeedSpinBox::mmPerMinChanged, this, changed);
    connect(maxSpeed_, &SpeedSpinBox::mmPerMinChanged, this, changed);
    connect(accelX_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(accelY_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(correctionX_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(correctionY_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(start_, &QPlainTextEdit::textChanged, this, changed);
    connect(end_, &QPlainTextEdit::textChanged, this, changed);
    connect(moveToZeroBefore_, &QCheckBox::toggled, this, changed);
    connect(returnToZeroAfter_, &QCheckBox::toggled, this, changed);
    connect(jogWithJogCommand_, &QCheckBox::toggled, this, changed);
}

void MachineProfilesDialog::refreshList(int select)
{
    loading_ = true;
    list_->clear();
    for (const Entry& entry : entries_) {
        list_->addItem(entry.setup.name);
    }
    loading_ = false;
    list_->setCurrentRow(qBound(0, select, static_cast<int>(entries_.size()) - 1));
}

void MachineProfilesDialog::showEntry(int index)
{
    if (loading_ || index < 0 || index >= static_cast<int>(entries_.size())) {
        return;
    }
    current_ = index;
    const MachineSetup& s = entries_[static_cast<size_t>(index)].setup;
    loading_ = true;
    nameLabel_->setText(s.name);
    workAreaX_->setMm(s.workAreaX);
    workAreaY_->setMm(s.workAreaY);
    rotation_->setCurrentIndex(rotation_->findData(s.rotationDeg));
    mirror_->setChecked(s.mirrorX);
    hasZ_->setChecked(s.hasZAxis);
    hasEndstops_->setChecked(s.hasEndstops);
    dpi_->setValue(s.laserDpi);
    pixelSizeLabel_->setText(tr("Pixelgröße %1").arg(formatLength(ld::core::defaultPixelSizeMm(s), 4)));
    command_->setCurrentIndex(command_->findData(static_cast<int>(s.laserCommand)));
    maxPower_->setValue(s.maxPower);
    rapid_->setMmPerMin(s.rapidMmPerMin);
    maxSpeed_->setMmPerMin(s.maxSpeedMmPerMin);
    accelX_->setValue(s.accelerationX);
    accelY_->setValue(s.accelerationY);
    correctionX_->setValue(s.correctionX);
    correctionY_->setValue(s.correctionY);
    start_->setPlainText(s.startCode);
    end_->setPlainText(s.endCode);
    moveToZeroBefore_->setChecked(s.moveToZeroBeforeStart);
    returnToZeroAfter_->setChecked(s.returnToZeroAfterJob);
    jogWithJogCommand_->setChecked(s.jogWithJogCommand);
    loading_ = false;
}

void MachineProfilesDialog::readForm()
{
    if (loading_ || current_ < 0 || current_ >= static_cast<int>(entries_.size())) {
        return;
    }
    MachineSetup& s = entries_[static_cast<size_t>(current_)].setup;
    s.workAreaX = workAreaX_->mm();
    s.workAreaY = workAreaY_->mm();
    s.rotationDeg = rotation_->currentData().toInt();
    s.mirrorX = mirror_->isChecked();
    s.hasZAxis = hasZ_->isChecked();
    s.hasEndstops = hasEndstops_->isChecked();
    s.laserDpi = dpi_->value();
    s.laserCommand = static_cast<LaserCommand>(command_->currentData().toInt());
    s.maxPower = maxPower_->value();
    s.rapidMmPerMin = rapid_->mmPerMin();
    s.maxSpeedMmPerMin = maxSpeed_->mmPerMin();
    s.accelerationX = accelX_->value();
    s.accelerationY = accelY_->value();
    s.correctionX = correctionX_->value();
    s.correctionY = correctionY_->value();
    s.startCode = start_->toPlainText();
    s.endCode = end_->toPlainText();
    s.moveToZeroBeforeStart = moveToZeroBefore_->isChecked();
    s.returnToZeroAfterJob = returnToZeroAfter_->isChecked();
    s.jogWithJogCommand = jogWithJogCommand_->isChecked();
    pixelSizeLabel_->setText(tr("Pixelgröße %1").arg(formatLength(ld::core::defaultPixelSizeMm(s), 4)));
}

bool MachineProfilesDialog::nameTaken(const QString& name, int exceptIndex) const
{
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (static_cast<int>(i) != exceptIndex &&
            QString::compare(entries_[i].setup.name, name.trimmed(), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString MachineProfilesDialog::uniqueName(const QString& base) const
{
    QString name = base;
    for (int i = 2; nameTaken(name, -1); ++i) {
        name = tr("%1 (%2)").arg(base).arg(i);
    }
    return name;
}

void MachineProfilesDialog::newProfile()
{
    MachineSetup s;
    s.name = uniqueName(tr("Neues Profil"));
    entries_.push_back({QString(), s});
    refreshList(static_cast<int>(entries_.size()) - 1);
}

void MachineProfilesDialog::copyProfile()
{
    if (current_ < 0) {
        return;
    }
    MachineSetup s = entries_[static_cast<size_t>(current_)].setup;
    s.name = uniqueName(tr("%1 (Kopie)").arg(s.name));
    entries_.push_back({QString(), s});
    refreshList(static_cast<int>(entries_.size()) - 1);
}

void MachineProfilesDialog::renameProfile()
{
    if (current_ < 0) {
        return;
    }
    Entry& entry = entries_[static_cast<size_t>(current_)];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Profil umbenennen"), tr("Neuer Name:"), QLineEdit::Normal,
                                               entry.setup.name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == entry.setup.name) {
        return;
    }
    if (nameTaken(name, current_)) {
        QMessageBox::warning(this, tr("Profil umbenennen"), tr("Es gibt schon ein Profil mit diesem Namen."));
        return;
    }
    entry.setup.name = name;
    refreshList(current_);
}

void MachineProfilesDialog::deleteProfile()
{
    if (current_ < 0) {
        return;
    }
    if (entries_.size() <= 1) {
        QMessageBox::information(this, tr("Profil löschen"), tr("Das letzte Profil kann nicht gelöscht werden."));
        return;
    }
    const Entry& entry = entries_[static_cast<size_t>(current_)];
    if (QMessageBox::question(this, tr("Profil löschen"),
                              tr("Das Profil \"%1\" wirklich löschen?").arg(entry.setup.name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    if (!entry.originalName.isEmpty()) {
        deleted_.append(entry.originalName);
    }
    entries_.erase(entries_.begin() + current_);
    refreshList(qMax(0, current_ - 1));
}

void MachineProfilesDialog::accept()
{
    // Das aktive Profil bleibt aktiv (auch umbenannt); wurde es gelöscht, gilt das gewählte.
    activeAfter_ = current_ >= 0 ? entries_[static_cast<size_t>(current_)].setup.name : QString();
    for (const Entry& entry : entries_) {
        if (QString::compare(entry.originalName, activeBefore_, Qt::CaseInsensitive) == 0 && !entry.originalName.isEmpty()) {
            activeAfter_ = entry.setup.name;
        }
    }

    QString error;
    for (const QString& name : deleted_) {
        profiles_->remove(name);
    }
    // Erst umbenennen (damit kein Name zwischendurch doppelt belegt ist), dann alles schreiben.
    for (const Entry& entry : entries_) {
        if (!entry.originalName.isEmpty() && entry.originalName != entry.setup.name &&
            !profiles_->rename(entry.originalName, entry.setup.name, &error)) {
            QMessageBox::warning(this, tr("Maschinenprofile"), error);
            return;
        }
    }
    for (const Entry& entry : entries_) {
        if (!profiles_->save(entry.setup, &error)) {
            QMessageBox::warning(this, tr("Maschinenprofile"), error);
            return;
        }
    }
    QDialog::accept();
}

}  // namespace ld::app
