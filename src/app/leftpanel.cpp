#include "leftpanel.h"

#include "appsettings.h"

#include <QButtonGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QVBoxLayout>

#include "accordion.h"
#include "segmentstyle.h"
#include "core/imagedocument.h"
#include "core/material.h"

namespace ld::app {

namespace {

// Kleiner Hilfsbutton für die Jog-Tasten (feste Größe, noch ohne Funktion).
QPushButton* makeJogButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setFixedSize(40, 30);
    button->setEnabled(false);  // M4: wird mit der GRBL-Verbindung aktiviert
    return button;
}

}  // namespace

LeftPanel::LeftPanel(ld::core::ImageDocument* document, const QString& materialFolder, QWidget* parent)
    : QWidget(parent), document_(document), materials_(materialFolder)
{
    // Inhalt: eine senkrechte Liste von Akkordeon-Abschnitten, in einer Scroll-Fläche,
    // damit auf kleinen Bildschirmen gescrollt werden kann.
    auto* content = new QWidget;
    auto* list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);

    // ---------------------------------------------------------------- Verbindung
    auto* connection =
        new AccordionSection(QStringLiteral("left/connection"), tr("Verbindung"), true);
    {
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);

        profileCombo_ = new QComboBox;
        form->addRow(tr("Profil"), profileCombo_);
        connect(profileCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (!updatingProfiles_ && index >= 0) {
                emit profileSelected(profileCombo_->itemText(index));
            }
        });

        portCombo_ = new PortComboBox;
        form->addRow(tr("Port"), portCombo_);
        connect(portCombo_, &PortComboBox::aboutToOpen, this, &LeftPanel::portListRequested);

        baudCombo_ = new QComboBox;
        for (const int baud : {9600, 19200, 38400, 57600, 115200, 230400, 250000, 460800, 921600}) {
            baudCombo_->addItem(QString::number(baud), baud);
        }
        baudCombo_->setCurrentIndex(baudCombo_->findData(115200));
        form->addRow(tr("Baud"), baudCombo_);
        connect(baudCombo_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updatingProfiles_) emit baudChanged(baudCombo_->currentData().toInt());
        });
        connection->contentLayout()->addLayout(form);

        auto* buttons = new QHBoxLayout;
        connectButton_ = new QPushButton(tr("Verbinden"));
        disconnectButton_ = new QPushButton(tr("Trennen"));
        disconnectButton_->setEnabled(false);
        buttons->addWidget(connectButton_);
        buttons->addWidget(disconnectButton_);
        connection->contentLayout()->addLayout(buttons);
        connect(connectButton_, &QPushButton::clicked, this, [this] {
            if (portCombo_->currentIndex() >= 0) {
                emit connectRequested(currentPortKey(), baudCombo_->currentData().toInt());
            }
        });
        connect(disconnectButton_, &QPushButton::clicked, this, &LeftPanel::disconnectRequested);
        baudTestButton_ = new QPushButton(tr("Baudrate testen …"));
        baudTestButton_->setToolTip(tr("Probiert gängige Baudraten aus, bis GRBL antwortet"));
        connection->contentLayout()->addWidget(baudTestButton_);
        connect(baudTestButton_, &QPushButton::clicked, this, [this] { emit baudTestRequested(currentPortKey()); });
    }
    list->addWidget(connection);

    // ---------------------------------------------------------------- Steuerung
    auto* control =
        new AccordionSection(QStringLiteral("left/control"), tr("Steuerung"), true);
    {
        // Kreuz aus Jog-Tasten: Y+ oben, X- links, X+ rechts, Y- unten.
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        auto* yPlus = makeJogButton(QStringLiteral("Y+"), this);
        auto* xMinus = makeJogButton(QStringLiteral("X-"), this);
        auto* xPlus = makeJogButton(QStringLiteral("X+"), this);
        auto* yMinus = makeJogButton(QStringLiteral("Y-"), this);
        jogButtons_ = {yPlus, xMinus, xPlus, yMinus};
        // Richtung aus Sicht des Bedieners: Y+ = vom Bediener weg.
        const std::pair<QPushButton*, QPointF> directions[] = {
            {yPlus, QPointF(0, 1)}, {xMinus, QPointF(-1, 0)}, {xPlus, QPointF(1, 0)}, {yMinus, QPointF(0, -1)}};
        for (const auto& [button, direction] : directions) {
            connect(button, &QPushButton::pressed, this, [this, direction] { emit jogPressed(direction); });
            connect(button, &QPushButton::released, this, &LeftPanel::jogReleased);
        }
        grid->addWidget(yPlus, 0, 1);
        grid->addWidget(xMinus, 1, 0);
        auto* center = new QLabel(QStringLiteral("●"));
        center->setAlignment(Qt::AlignCenter);
        grid->addWidget(center, 1, 1);
        grid->addWidget(xPlus, 1, 2);
        grid->addWidget(yMinus, 2, 1);
        // Z-Achse rechts neben dem Kreuz (Z+ auf der Höhe von Y+, Z- auf der Höhe von Y-). Der Laser hat keine
        // Z-Achse; die Tasten sind ausgegraut (später per Maschinenprofil), der Grund steht im Tooltip.
        grid->setColumnMinimumWidth(3, 14);
        auto* zPlus = makeJogButton(QStringLiteral("Z+"), this);
        auto* zMinus = makeJogButton(QStringLiteral("Z-"), this);
        zButtons_ = {zPlus, zMinus};
        grid->addWidget(zPlus, 0, 4);
        grid->addWidget(zMinus, 2, 4);

        // Das Raster in der Mitte zentrieren.
        auto* gridRow = new QHBoxLayout;
        gridRow->addStretch();
        gridRow->addLayout(grid);
        gridRow->addStretch();
        control->contentLayout()->addLayout(gridRow);

        // Schrittweite: vier Tasten, genau eine ist aktiv (Werte und Vorwahl aus den Einstellungen, Vorgabe 0,1 / 1 / 10 / 100, vorgewählt 10).
        auto* stepRow = new QHBoxLayout;
        stepRow->setSpacing(0);
        stepRow->addWidget(new QLabel(tr("Schritt")));
        stepRow->addSpacing(8);
        stepButtonsLayout_ = new QHBoxLayout;
        stepButtonsLayout_->setSpacing(0);
        stepRow->addLayout(stepButtonsLayout_);
        stepRow->addWidget(new QLabel(tr(" mm")));
        control->contentLayout()->addLayout(stepRow);
        stepGroup_ = new QButtonGroup(this);
        stepGroup_->setExclusive(true);
        setJogSteps(AppSettings::instance().jogSteps(), AppSettings::instance().jogDefaultIndex());

        positionLabel_ = new QLabel(tr("Position: –"));
        control->contentLayout()->addWidget(positionLabel_);

        auto* zeroRow = new QHBoxLayout;
        auto* setZero = new QPushButton(tr("Hier ist Null"));
        auto* goZero = new QPushButton(tr("Zum Nullpunkt"));
        setZero->setEnabled(false);
        goZero->setEnabled(false);
        zeroButtons_ = {setZero, goZero};
        setZero->setToolTip(tr("Die aktuelle Position wird der Nullpunkt (X0 Y0)"));
        goZero->setToolTip(tr("Im Eilgang zum Nullpunkt fahren, Laser aus"));
        connect(setZero, &QPushButton::clicked, this, &LeftPanel::setZeroRequested);
        connect(goZero, &QPushButton::clicked, this, &LeftPanel::goZeroRequested);
        zeroRow->addWidget(setZero);
        zeroRow->addWidget(goZero);
        control->contentLayout()->addLayout(zeroRow);

    }
    list->addWidget(control);

    // ---------------------------------------------------------------- Schwachlicht
    auto* weak =
        new AccordionSection(QStringLiteral("left/weaklight"), tr("Schwachlicht"), false);
    {
        weakToggle_ = new QPushButton(tr("Schwachlicht einschalten"));
        weakToggle_->setCheckable(true);
        weakToggle_->setEnabled(false);   // nur mit Verbindung, Bereitschaft und ohne Auftrag
        weak->contentLayout()->addWidget(weakToggle_);
        connect(weakToggle_, &QPushButton::clicked, this, [this](bool checked) { emit weakLightToggled(checked); });

        auto* powerRow = new QHBoxLayout;
        powerRow->addWidget(new QLabel(tr("Power")));
        weakPower_ = new QSlider(Qt::Horizontal);
        weakPower_->setRange(1, 5);
        weakPower_->setValue(3);
        weakPowerValue_ = new QLabel(tr("3 %"));
        weakPowerValue_->setMinimumWidth(36);
        connect(weakPower_, &QSlider::valueChanged, this, [this](int value) {
            weakPowerValue_->setText(tr("%1 %").arg(value));
            if (!updatingWeak_) emit weakLightPowerChanged(value);
        });
        powerRow->addWidget(weakPower_, 1);
        powerRow->addWidget(weakPowerValue_);
        weak->contentLayout()->addLayout(powerRow);

        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        weakAutoOff_ = new QComboBox;
        for (const int seconds : {10, 30, 60, 120, 0}) {
            weakAutoOff_->addItem(seconds == 0 ? tr("nie") : seconds >= 60 ? tr("nach %1 min").arg(seconds / 60) : tr("nach %1 s").arg(seconds), seconds);
        }
        weakAutoOff_->setCurrentIndex(1);
        connect(weakAutoOff_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updatingWeak_) emit weakLightAutoOffChanged(weakAutoOff_->currentData().toInt());
        });
        form->addRow(tr("Auto-Aus"), weakAutoOff_);
        weak->contentLayout()->addLayout(form);
    }
    list->addWidget(weak);

    // ---------------------------------------------------------------- Material
    auto* material =
        new AccordionSection(QStringLiteral("left/material"), tr("Material"), false);
    buildMaterialSection(material);
    list->addWidget(material);
    materialSection_ = material;

    list->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    // Auswahllisten sollen sich nicht auf ihren längsten Eintrag verbreitern, sonst bestimmt
    // "Absolute Koordinaten" die Breite der ganzen Leiste. Längere Einträge werden gekürzt
    // angezeigt, die volle Liste klappt beim Öffnen auf.
    for (QComboBox* combo : content->findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(10);
    }

    // Mindestbreite = Breite, die der Inhalt braucht (plus senkrechte Bildlaufleiste).
    // Ohne diese Zeile darf die Leiste schmaler werden als ihr Inhalt, und weil die
    // waagerechte Bildlaufleiste aus ist, würde der Überstand einfach abgeschnitten.
    scroll->setMinimumWidth(content->minimumSizeHint().width()
                            + scroll->verticalScrollBar()->sizeHint().width() + 4);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void LeftPanel::setProfiles(const QStringList& names, const QString& active)
{
    updatingProfiles_ = true;
    profileCombo_->clear();
    profileCombo_->addItems(names);
    profileCombo_->setCurrentIndex(qMax(0, names.indexOf(active)));
    updatingProfiles_ = false;
}

void LeftPanel::setPortChoices(const QList<PortChoice>& choices, const QString& select)
{
    const QString keep = select.isEmpty() ? currentPortKey() : select;
    const QSignalBlocker blocker(portCombo_);
    portCombo_->clear();
    for (const PortChoice& choice : choices) portCombo_->addItem(choice.text, choice.key);
    const int index = portCombo_->findData(keep);
    portCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

QString LeftPanel::currentPortKey() const
{
    return portCombo_->currentData().toString();
}

void LeftPanel::setBaudRate(int baud)
{
    const QSignalBlocker blocker(baudCombo_);
    int index = baudCombo_->findData(baud);
    if (index < 0) {
        baudCombo_->addItem(QString::number(baud), baud);   // ungewöhnlicher Wert aus dem Profil
        index = baudCombo_->findData(baud);
    }
    baudCombo_->setCurrentIndex(index);
}

void LeftPanel::setConnectionState(ld::grbl::GrblConnection::State state)
{
    const bool disconnected = state == ld::grbl::GrblConnection::State::Disconnected;
    connectButton_->setEnabled(disconnected);
    disconnectButton_->setEnabled(!disconnected);
    portCombo_->setEnabled(disconnected);
    baudCombo_->setEnabled(disconnected);
    baudTestButton_->setEnabled(disconnected);
}

// Baut die vier Schritt-Tasten neu auf (Einstellungen). Die vorgewählte Taste meldet ihren Wert über stepChanged().
void LeftPanel::setJogSteps(const QList<double>& steps, int selected)
{
    for (QAbstractButton* button : stepGroup_->buttons()) {
        stepGroup_->removeButton(button);
        delete button;
    }
    for (int i = 0; i < steps.size(); ++i) {
        const double stepMm = steps[i];
        auto* stepButton = new QPushButton(QLocale().toString(stepMm, 'g', 4));
        connect(stepButton, &QPushButton::clicked, this, [this, stepMm] { emit stepChanged(stepMm); });
        stepButton->setCheckable(true);
        applySegmentStyle(stepButton);
        stepButton->setMinimumWidth(40);
        stepButton->setChecked(i == selected);
        stepGroup_->addButton(stepButton);
        stepButtonsLayout_->addWidget(stepButton);
    }
    if (selected >= 0 && selected < steps.size()) emit stepChanged(steps[selected]);
}

void LeftPanel::setMotionEnabled(bool jog, bool zero)
{
    for (QPushButton* b : jogButtons_) b->setEnabled(jog);
    for (QPushButton* b : zeroButtons_) b->setEnabled(zero);
}

void LeftPanel::setPositionText(const QString& text)
{
    positionLabel_->setText(text);
}

void LeftPanel::setWeakLightRange(int maxPercent)
{
    updatingWeak_ = true;
    weakPower_->setRange(1, maxPercent);
    updatingWeak_ = false;
}

void LeftPanel::setWeakLightValues(int percent, int autoOffSeconds)
{
    updatingWeak_ = true;
    weakPower_->setValue(percent);
    weakPowerValue_->setText(tr("%1 %").arg(weakPower_->value()));
    const int index = weakAutoOff_->findData(autoOffSeconds);
    if (index >= 0) weakAutoOff_->setCurrentIndex(index);
    updatingWeak_ = false;
}

void LeftPanel::setWeakLightState(bool on, bool canToggle)
{
    const QSignalBlocker blocker(weakToggle_);
    weakToggle_->setChecked(on);
    weakToggle_->setText(on ? tr("Schwachlicht ausschalten") : tr("Schwachlicht einschalten"));
    weakToggle_->setEnabled(canToggle || on);
}

void LeftPanel::setHasZAxis(bool hasZAxis)
{
    // Die Z-Tasten bekommen ihre Funktion erst später; bis dahin erklärt der Tooltip, warum sie aus sind.
    for (QPushButton* button : zButtons_) {
        button->setToolTip(hasZAxis ? QString() : tr("Z ausgegraut: Das Profil hat keine Z-Achse"));
    }
}

// ---------------------------------------------------------------------------------------------
// Material (siehe docs/planung.md 3.12)
// ---------------------------------------------------------------------------------------------
void LeftPanel::setMaterialLocked(bool locked)
{
    if (materialSection_) materialSection_->setEnabled(!locked);
}

void LeftPanel::buildMaterialSection(AccordionSection* section)
{
    materialCombo_ = new QComboBox;
    section->contentLayout()->addWidget(materialCombo_);

    noteEdit_ = new QPlainTextEdit;
    noteEdit_->setPlaceholderText(tr("Notiz zum Material (zum Beispiel: mit Luftunterstützung)"));
    noteEdit_->setFixedHeight(54);
    section->contentLayout()->addWidget(noteEdit_);

    saveButton_ = new QPushButton(tr("Speichern"));
    saveAsButton_ = new QPushButton(tr("Speichern als …"));
    renameButton_ = new QPushButton(tr("Umbenennen …"));
    deleteButton_ = new QPushButton(tr("Löschen"));
    auto* test = new QPushButton(tr("Materialtest …"));
    connect(test, &QPushButton::clicked, this, &LeftPanel::materialTestRequested);
    for (QPushButton* button : {saveButton_, saveAsButton_, renameButton_, deleteButton_, test}) {
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    saveButton_->setToolTip(tr("Überschreibt das gewählte Material mit den aktuellen Gravur-Werten"));
    auto* row1 = new QHBoxLayout;
    row1->addWidget(saveButton_);
    row1->addWidget(saveAsButton_);
    auto* row2 = new QHBoxLayout;
    row2->addWidget(renameButton_);
    row2->addWidget(deleteButton_);
    auto* row3 = new QHBoxLayout;
    row3->addWidget(test);
    section->contentLayout()->addLayout(row1);
    section->contentLayout()->addLayout(row2);
    section->contentLayout()->addLayout(row3);

    connect(materialCombo_, &QComboBox::activated, this, [this] { loadSelectedMaterial(); });
    connect(noteEdit_, &QPlainTextEdit::textChanged, this, &LeftPanel::updateMaterialState);
    connect(document_, &ld::core::ImageDocument::engravingChanged, this, &LeftPanel::updateMaterialState);
    connect(saveButton_, &QPushButton::clicked, this, &LeftPanel::saveMaterial);
    connect(saveAsButton_, &QPushButton::clicked, this, &LeftPanel::saveMaterialAs);
    connect(renameButton_, &QPushButton::clicked, this, &LeftPanel::renameMaterial);
    connect(deleteButton_, &QPushButton::clicked, this, &LeftPanel::deleteMaterial);

    refreshMaterials(QString());
}

// Liste neu füllen; select = Name, der gewählt werden soll (leer = "kein Material").
void LeftPanel::refreshMaterials(const QString& select)
{
    const QSignalBlocker blocker(materialCombo_);
    materialCombo_->clear();
    materialCombo_->addItem(tr("(kein Material)"), QString());
    for (const QString& name : materials_.names()) {
        materialCombo_->addItem(name, name);
    }
    const int index = select.isEmpty() ? 0 : materialCombo_->findData(select);
    materialCombo_->setCurrentIndex(qMax(0, index));
    updateMaterialState();
}

void LeftPanel::loadSelectedMaterial()
{
    const QString name = materialCombo_->currentData().toString();
    if (name.isEmpty()) {
        loaded_.reset();
        {
            const QSignalBlocker blocker(noteEdit_);
            noteEdit_->clear();
        }
        updateMaterialState();
        return;
    }
    const auto json = materials_.read(name);
    const auto material = json ? ld::core::materialFromJson(*json) : std::nullopt;
    if (!material) {
        QMessageBox::warning(this, tr("Material"), tr("Das Material \"%1\" kann nicht gelesen werden.").arg(name));
        refreshMaterials(QString());
        loaded_.reset();
        return;
    }
    loaded_ = material;
    {
        const QSignalBlocker blocker(noteEdit_);
        noteEdit_->setPlainText(material->note);
    }
    document_->setOperation(ld::core::applyMaterial(document_->operation(), *material));
    updateMaterialState();
}

bool LeftPanel::isMaterialModified() const
{
    return loaded_ && (!ld::core::operationMatchesMaterial(document_->operation(), *loaded_) ||
                       noteEdit_->toPlainText() != loaded_->note);
}

void LeftPanel::updateMaterialState()
{
    // Der gewählte Eintrag zeigt "(geändert)", sobald Werte oder Notiz vom Gespeicherten abweichen.
    const int index = materialCombo_->currentIndex();
    if (index > 0) {
        const QString name = materialCombo_->itemData(index).toString();
        materialCombo_->setItemText(index, isMaterialModified() ? tr("%1 (geändert)").arg(name) : name);
    }
    renameButton_->setEnabled(loaded_.has_value());
    deleteButton_->setEnabled(loaded_.has_value());
}

void LeftPanel::saveMaterial()
{
    if (!loaded_) {
        saveMaterialAs();   // noch kein Material gewählt: unter neuem Namen speichern
        return;
    }
    const ld::core::MaterialProfile material =
        ld::core::materialFromOperation(loaded_->name, document_->operation(), noteEdit_->toPlainText());
    QString error;
    if (!materials_.write(ld::core::materialToJson(material), &error)) {
        QMessageBox::warning(this, tr("Material speichern"), error);
        return;
    }
    loaded_ = material;
    updateMaterialState();
}

void LeftPanel::saveMaterialAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Material speichern als"), tr("Name des Materials:"),
                                               QLineEdit::Normal, loaded_ ? loaded_->name : QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (materials_.exists(name) &&
        QMessageBox::question(this, tr("Material speichern"),
                              tr("Es gibt schon ein Material \"%1\". Überschreiben?").arg(name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    const ld::core::MaterialProfile material =
        ld::core::materialFromOperation(name, document_->operation(), noteEdit_->toPlainText());
    QString error;
    if (!materials_.write(ld::core::materialToJson(material), &error)) {
        QMessageBox::warning(this, tr("Material speichern"), error);
        return;
    }
    loaded_ = material;
    refreshMaterials(name);
}

void LeftPanel::saveMaterialFromOperation(const ld::core::Operation& operation, const QString& note)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Material speichern"), tr("Name des Materials:"),
                                               QLineEdit::Normal, loaded_ ? loaded_->name : QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (materials_.exists(name) &&
        QMessageBox::question(this, tr("Material speichern"),
                              tr("Es gibt schon ein Material \"%1\". Überschreiben?").arg(name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    const ld::core::MaterialProfile material = ld::core::materialFromOperation(name, operation, note);
    QString error;
    if (!materials_.write(ld::core::materialToJson(material), &error)) {
        QMessageBox::warning(this, tr("Material speichern"), error);
        return;
    }
    loaded_ = material;
    refreshMaterials(name);
}

void LeftPanel::renameMaterial()
{
    if (!loaded_) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Material umbenennen"), tr("Neuer Name:"),
                                               QLineEdit::Normal, loaded_->name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == loaded_->name) {
        return;
    }
    QString error;
    if (!materials_.rename(loaded_->name, name, &error)) {
        QMessageBox::warning(this, tr("Material umbenennen"), error);
        return;
    }
    loaded_->name = name;
    refreshMaterials(name);
}

void LeftPanel::deleteMaterial()
{
    if (!loaded_) {
        return;
    }
    if (QMessageBox::question(this, tr("Material löschen"),
                              tr("Das Material \"%1\" wirklich löschen?").arg(loaded_->name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    materials_.remove(loaded_->name);
    loaded_.reset();
    {
        const QSignalBlocker blocker(noteEdit_);
        noteEdit_->clear();
    }
    refreshMaterials(QString());
}

}  // namespace ld::app
