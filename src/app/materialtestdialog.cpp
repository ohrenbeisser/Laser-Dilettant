#include "materialtestdialog.h"
#include "helpwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "appsettings.h"
#include "core/gcode.h"
#include "segmentstyle.h"
#include "unitspinboxes.h"

namespace ld::app {

using ld::core::EngraveMode;
using ld::core::TestMotif;

namespace {

constexpr int kTile = 52;   // Kantenlänge eines Feldes in der Tabelle (Bildschirmpixel)

// Gruppe von Umschaltknöpfen (genau einer aktiv) mit gemeinsamem Stil; id ist der Wert.
QWidget* segmentRow(QButtonGroup* group, const QList<QPair<QString, int>>& entries, int current, QWidget* parent)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    group->setExclusive(true);
    for (const auto& [text, id] : entries) {
        auto* button = new QPushButton(text);
        button->setCheckable(true);
        applySegmentStyle(button);
        group->addButton(button, id);
        layout->addWidget(button);
        button->setChecked(id == current);
    }
    return widget;
}

// Auswahlfeld aus (Text, Wert)-Paaren; current ist der Wert, der anfangs gewählt ist.
QComboBox* makeCombo(const QList<QPair<QString, int>>& entries, int current)
{
    auto* combo = new QComboBox;
    for (const auto& [text, id] : entries) combo->addItem(text, id);
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    return combo;
}

}  // namespace

MaterialTestDialog::MaterialTestDialog(const ld::core::MachineSetup& machine, const ld::core::Operation& operation,
                                       ld::core::JobOrigin origin, double pixelSizeMm, QWidget* parent)
    : QDialog(parent), machine_(machine), operation_(operation), origin_(origin), pixelSizeMm_(pixelSizeMm)
{
    setWindowTitle(tr("Materialtest"));
    attachHelp(this, ld::core::helptopic::kMaterialTest);   // F1 und Knopf "Hilfe" (Handbuch)
    mode_ = operation.mode;
    summaryTimer_.setSingleShot(true);
    summaryTimer_.setInterval(350);
    connect(&summaryTimer_, &QTimer::timeout, this, &MaterialTestDialog::updateSummary);
    buildUi();
    applyDefaults(false);
    rebuildTable();
    updateSummary();
}

bool MaterialTestDialog::cutGroup() const { return ld::core::isCutMode(mode_); }

// Im Cutting ist die Auflösung ausgegraut (wie im rechten Panel des Hauptfensters).
void MaterialTestDialog::updateResolutionEnabled()
{
    resolutionSpin_->setEnabled(!cutGroup());
    resolutionLabel_->setEnabled(!cutGroup());
}

void MaterialTestDialog::buildUi()
{
    // Zwei Bereiche nebeneinander: links die Tabelle, rechts Auswahl, Beschriftung, Übersicht und Knöpfe.
    auto* main = new QHBoxLayout(this);

    // ---------------------------------------------------------------- links: Tabelle (wird in rebuildTable() gefüllt)
    auto* tableGroup = new QGroupBox(tr("Testtabelle"));
    auto* tableLayout = new QVBoxLayout(tableGroup);
    auto* tableBox = new QWidget;
    grid_ = new QGridLayout(tableBox);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(6);
    grid_->setVerticalSpacing(6);
    tableLayout->addWidget(tableBox);
    summary_ = new QLabel;   // Hinweis zur Power (steht unter der Tabelle)
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    tableLayout->addWidget(summary_);
    tableLayout->addStretch(1);
    main->addWidget(tableGroup, 1);
    tileGroup_ = new QButtonGroup(this);
    tileGroup_->setExclusive(true);

    // ---------------------------------------------------------------- rechts
    auto* side = new QVBoxLayout;
    main->addLayout(side);

    // Gruppe "Motiv": jede Auswahl mit der Beschriftung darüber, damit die Reihen schmal bleiben.
    // Der Modus steht ganz oben als Umschaltknöpfe.
    modeGroup_ = new QButtonGroup(this);
    side->addWidget(segmentRow(modeGroup_, {{tr("Full"), static_cast<int>(EngraveMode::Full)},
                                            {tr("Cutting"), static_cast<int>(EngraveMode::Cutting)},
                                            {tr("Linie"), static_cast<int>(EngraveMode::Line)}},
                               static_cast<int>(mode_), this));
    auto* motifGroup = new QGroupBox(tr("Motiv"));
    cellsCombo_ = makeCombo({{QStringLiteral("3 × 3"), 3}, {QStringLiteral("4 × 4"), 4}, {QStringLiteral("5 × 5"), 5}}, cells_);
    motifCombo_ = new QComboBox;
    motifCombo_->addItem(tr("Kreis gefüllt"), static_cast<int>(TestMotif::CircleFilled));
    motifCombo_->addItem(tr("Kreis (Umriss)"), static_cast<int>(TestMotif::CircleOutline));
    motifCombo_->addItem(tr("Quadrat gefüllt"), static_cast<int>(TestMotif::SquareFilled));
    motifCombo_->addItem(tr("Quadrat (Umriss)"), static_cast<int>(TestMotif::SquareOutline));
    motifCombo_->addItem(tr("Quadrat mit Verlauf (diagonale Linien)"), static_cast<int>(TestMotif::SquareGradient));
    motifCombo_->setCurrentIndex(motifCombo_->findData(static_cast<int>(motif_)));
    sizeCombo_ = makeCombo({{QStringLiteral("5 mm"), 5}, {QStringLiteral("10 mm"), 10}, {QStringLiteral("15 mm"), 15}, {QStringLiteral("20 mm"), 20}},
                           static_cast<int>(sizeMm_));
    // Auflösung des Lasers (Linien/mm, dpi oder Pixelgröße nach den Einstellungen); Vorgabe: die des Bildes
    resolutionSpin_ = new ResolutionSpinBox(0.01, 1.0);
    resolutionSpin_->setMm(pixelSizeMm_);
    resolutionLabel_ = new QLabel(resolutionLabel());
    resolutionSpin_->setToolTip(tr("Rasterabstand für Motive und Zahlen. Gilt nur für diesen Test."));
    auto* choices = new QFormLayout(motifGroup);
    choices->addRow(tr("Tabelle"), cellsCombo_);
    choices->addRow(tr("Motiv"), motifCombo_);
    choices->addRow(tr("Größe des Motivs"), sizeCombo_);
    choices->addRow(resolutionLabel_, resolutionSpin_);
    side->addWidget(motifGroup);

    // Gruppe "Beschriftung": Speed und Power, mit denen die Zahlen am Werkstück gebrannt werden.
    auto* labelGroup = new QGroupBox(tr("Beschriftung"));
    auto* labelForm = new QFormLayout(labelGroup);
    labelSpeed_ = new SpeedSpinBox(10.0, std::max(10.0, machine_.maxSpeedMmPerMin));   // nie über der Grenze des Profils
    // Die Zahlen sind Gravur: Speed und Power kommen aus den Gravur-Werten des Hauptprogramms.
    const ld::core::ModeValues engrave = ld::core::engraveValuesOf(operation_);
    labelSpeed_->setMmPerMin(std::max(10.0, engrave.speedMmPerMin));
    labelForm->addRow(tr("Speed"), labelSpeed_);
    labelPower_ = new QDoubleSpinBox;
    labelPower_->setRange(0.0, 100.0);
    labelPower_->setDecimals(0);
    labelPower_->setSuffix(tr(" %"));
    labelPower_->setValue(std::clamp(std::round(engrave.powerPercent), 0.0, 100.0));
    labelForm->addRow(tr("Power"), labelPower_);
    side->addWidget(labelGroup);
    connect(labelSpeed_, &SpeedSpinBox::mmPerMinChanged, this, &MaterialTestDialog::scheduleSummary);
    connect(labelPower_, &QDoubleSpinBox::valueChanged, this, &MaterialTestDialog::scheduleSummary);

    // ---------------------------------------------------------------- Übersicht
    auto* summaryGroup = new QGroupBox(tr("Übersicht"));
    auto* summaryForm = new QFormLayout(summaryGroup);
    sizeLabel_ = new QLabel;
    linesLabel_ = new QLabel;
    durationLabel_ = new QLabel;
    summaryForm->addRow(tr("Gesamtgröße"), sizeLabel_);
    summaryForm->addRow(tr("G-Code"), linesLabel_);
    summaryForm->addRow(tr("Dauer"), durationLabel_);
    warning_ = new QLabel;
    warning_->setWordWrap(true);
    warning_->setStyleSheet(QStringLiteral("color: #d42a29; font-weight: 600;"));
    warning_->hide();
    summaryForm->addRow(warning_);
    side->addWidget(summaryGroup);
    side->addStretch(1);

    // ---------------------------------------------------------------- Knöpfe
    frameButton_ = new QPushButton(tr("Rahmen"));
    frameButton_->setToolTip(tr("Fährt den Rahmen um die ganze Zeichnung ab (mit den Werten aus dem Abschnitt Rahmen)"));
    startButton_ = new QPushButton(tr("Start"));
    startButton_->setToolTip(tr("Brennt den Materialtest. Der Laser muss verbunden und bereit sein."));
    startButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(highlight); color: palette(highlighted-text); font-weight: 600; border-radius: 3px; padding: 6px 14px; }"
        "QPushButton:disabled { background: palette(mid); color: palette(button); }"));
    saveGcodeButton_ = new QPushButton(tr("G-Code speichern …"));
    saveMaterialButton_ = new QPushButton(tr("Als Material speichern …"));
    saveMaterialButton_->setToolTip(tr("Zuerst ein Feld anklicken: Seine Werte (Speed und Power) kommen ins Material."));
    auto* close = new QPushButton(tr("Schließen"));
    auto* frameStart = new QHBoxLayout;
    frameStart->addWidget(frameButton_);
    frameStart->addWidget(startButton_, 1);
    side->addLayout(frameStart);
    side->addWidget(saveGcodeButton_);
    side->addWidget(saveMaterialButton_);
    side->addSpacing(8);
    side->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(frameButton_, &QPushButton::clicked, this, [this] { emit frameRequested(spec()); });
    connect(startButton_, &QPushButton::clicked, this, [this] { emit startRequested(spec()); });
    connect(saveGcodeButton_, &QPushButton::clicked, this, [this] { emit saveGcodeRequested(spec()); });
    connect(saveMaterialButton_, &QPushButton::clicked, this, [this] {
        const int c = selectedColumn(), r = selectedRow();
        if (c < 0 || r < 0) return;
        emit saveMaterialRequested(speedSpins_[r]->mmPerMin(), powerSpins_[c]->value(), mode_);
    });

    // ---------------------------------------------------------------- Reaktionen auf die Auswahl
    connect(cellsCombo_, &QComboBox::currentIndexChanged, this, [this] {
        cells_ = cellsCombo_->currentData().toInt();
        rebuildTable();
    });
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        sizeMm_ = sizeCombo_->currentData().toInt();
        updateTiles();
        scheduleSummary();
    });
    connect(modeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        const bool wasCut = cutGroup();
        mode_ = static_cast<EngraveMode>(id);
        updateResolutionEnabled();
        if (cutGroup() != wasCut) {
            applyDefaults(false);   // Gravur und Schnitt haben andere Werte
            rebuildTable(false);
        }
        scheduleSummary();
    });
    connect(resolutionSpin_, &ResolutionSpinBox::mmChanged, this, [this](double mm) {
        pixelSizeMm_ = mm;
        updateTiles();
        scheduleSummary();
    });
    updateResolutionEnabled();
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, [this] { resolutionLabel_->setText(resolutionLabel()); });
    connect(motifCombo_, &QComboBox::currentIndexChanged, this, [this] {
        motif_ = static_cast<TestMotif>(motifCombo_->currentData().toInt());
        updateTiles();
        scheduleSummary();
    });
    connect(tileGroup_, &QButtonGroup::buttonToggled, this, [this] { updateButtons(); });
    updateButtons();
}

// Vorbelegung: Speed von 50 bis 100 % der aktuellen Speed, Power von 20 bis 100 % (aus dem aktuellen Material).
void MaterialTestDialog::applyDefaults(bool keepEnds)
{
    const ld::core::ModeValues values = cutGroup() ? ld::core::cutValuesOf(operation_) : ld::core::engraveValuesOf(operation_);
    double speedFirst = std::round(values.speedMmPerMin * 0.5 / 10.0) * 10.0;
    double speedLast = std::round(values.speedMmPerMin / 10.0) * 10.0;
    double powerFirst = 20.0, powerLast = 100.0;
    if (keepEnds && !speedSpins_.isEmpty() && !powerSpins_.isEmpty()) {
        speedFirst = speedSpins_.first()->mmPerMin();
        speedLast = speedSpins_.last()->mmPerMin();
        powerFirst = powerSpins_.first()->value();
        powerLast = powerSpins_.last()->value();
    }
    startSpeed_ = std::max(10.0, speedFirst);
    endSpeed_ = std::max(10.0, speedLast);
    startPower_ = powerFirst;
    endPower_ = powerLast;
}

void MaterialTestDialog::rebuildTable(bool keepEnds)
{
    rebuilding_ = true;
    if (keepEnds && !speedSpins_.isEmpty()) {   // beim Wechsel der Größe: erster und letzter Wert bleiben
        applyDefaults(true);
    }
    // alte Widgets entfernen
    while (QLayoutItem* item = grid_->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            if (auto* button = qobject_cast<QToolButton*>(w)) tileGroup_->removeButton(button);
            w->deleteLater();
        }
        delete item;
    }
    speedSpins_.clear();
    powerSpins_.clear();
    tiles_.clear();

    auto* corner = new QLabel(tr("Speed ↓ / Power →"));
    corner->setWordWrap(true);
    corner->setEnabled(false);
    grid_->addWidget(corner, 0, 0);
    // Kopfzeile: Power je Spalte
    for (int i = 0; i < cells_; ++i) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(0.0, 100.0);
        spin->setDecimals(0);
        spin->setSingleStep(5.0);
        spin->setSuffix(tr(" %"));
        spin->setAlignment(Qt::AlignHCenter);
        const double t = cells_ > 1 ? static_cast<double>(i) / (cells_ - 1) : 0.0;
        spin->setValue(std::round(startPower_ + (endPower_ - startPower_) * t));
        spin->setToolTip(tr("Power dieser Spalte"));
        powerSpins_.append(spin);
        grid_->addWidget(spin, 0, i + 1);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &MaterialTestDialog::scheduleSummary);
    }
    // linke Spalte: Speed je Zeile; dazu die Felder
    for (int j = 0; j < cells_; ++j) {
        auto* spin = new SpeedSpinBox(10.0, std::max(10.0, machine_.maxSpeedMmPerMin));
        const double t = cells_ > 1 ? static_cast<double>(j) / (cells_ - 1) : 0.0;
        spin->setMmPerMin(std::round((startSpeed_ + (endSpeed_ - startSpeed_) * t) / 10.0) * 10.0);
        spin->setToolTip(tr("Speed dieser Zeile"));
        speedSpins_.append(spin);
        grid_->addWidget(spin, j + 1, 0);
        connect(spin, &SpeedSpinBox::mmPerMinChanged, this, &MaterialTestDialog::scheduleSummary);
        for (int i = 0; i < cells_; ++i) {
            auto* tile = new QToolButton;
            tile->setCheckable(true);
            tile->setFixedSize(kTile + 10, kTile + 10);
            tile->setIconSize(QSize(kTile, kTile));
            tile->setToolTip(tr("Feld anklicken, um es als Material zu speichern"));
            tile->setStyleSheet(QStringLiteral(
                "QToolButton:checked { background: palette(highlight); border: 2px solid palette(highlight); }"));
            tileGroup_->addButton(tile);
            tiles_.append(tile);
            grid_->addWidget(tile, j + 1, i + 1);
        }
    }
    // "Verteilen" direkt unter der Spalte bzw. der Kopfzeile, auf die es wirkt
    auto* spreadSpeed = new QPushButton(tr("Speed verteilen"));
    spreadSpeed->setToolTip(tr("Verteilt die Speed-Werte gleichmäßig zwischen dem ersten und dem letzten Wert."));
    grid_->addWidget(spreadSpeed, cells_ + 1, 0);
    connect(spreadSpeed, &QPushButton::clicked, this, [this] { spread(true); });
    auto* spreadPower = new QPushButton(tr("Power verteilen"));
    spreadPower->setToolTip(tr("Verteilt die Power-Werte gleichmäßig zwischen dem ersten und dem letzten Wert."));
    grid_->addWidget(spreadPower, cells_ + 1, 1, 1, cells_);
    connect(spreadPower, &QPushButton::clicked, this, [this] { spread(false); });
    rebuilding_ = false;
    updateTiles();
    updateButtons();
    scheduleSummary();
}

void MaterialTestDialog::updateTiles()
{
    const QImage raster = ld::core::motifRaster(motif_, sizeMm_, pixelSizeMm_);
    QImage shown = raster.scaled(kTile, kTile, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    // Weiß wird durchsichtig, damit das Symbol zum Design (hell/dunkel) passt.
    const QColor ink = palette().color(QPalette::ButtonText);
    for (int y = 0; y < shown.height(); ++y) {
        for (int x = 0; x < shown.width(); ++x) {
            const int gray = qGray(shown.pixel(x, y));
            QColor c = ink;
            c.setAlpha(255 - gray);
            shown.setPixelColor(x, y, c);
        }
    }
    const QIcon icon(QPixmap::fromImage(shown));
    for (QToolButton* tile : tiles_) tile->setIcon(icon);
}

void MaterialTestDialog::spread(bool speeds)
{
    if (speeds) {
        if (speedSpins_.size() < 2) return;
        const double a = speedSpins_.first()->mmPerMin(), b = speedSpins_.last()->mmPerMin();
        for (int i = 0; i < speedSpins_.size(); ++i) {
            speedSpins_[i]->setMmPerMin(std::round((a + (b - a) * i / (speedSpins_.size() - 1)) / 10.0) * 10.0);
        }
    } else {
        if (powerSpins_.size() < 2) return;
        const double a = powerSpins_.first()->value(), b = powerSpins_.last()->value();
        for (int j = 0; j < powerSpins_.size(); ++j) {
            powerSpins_[j]->setValue(std::round(a + (b - a) * j / (powerSpins_.size() - 1)));
        }
    }
    scheduleSummary();
}

QString MaterialTestDialog::speedLabel(double mmPerMin) const
{
    if (AppSettings::instance().speedUnit() == SpeedUnit::MmPerSec) {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(mmPerMin / 60.0, 'f', 1);
    }
    return QString::number(static_cast<int>(std::lround(mmPerMin)));
}

ld::core::MaterialTestSpec MaterialTestDialog::spec() const
{
    ld::core::MaterialTestSpec s;
    s.cells = cells_;
    s.motif = motif_;
    s.sizeMm = sizeMm_;
    s.mode = mode_;
    for (SpeedSpinBox* spin : speedSpins_) {
        s.speeds.push_back(spin->mmPerMin());
        s.speedLabels.push_back(speedLabel(spin->mmPerMin()));
    }
    for (QDoubleSpinBox* spin : powerSpins_) {
        s.powers.push_back(spin->value());
        s.powerLabels.push_back(QString::number(static_cast<int>(std::lround(spin->value()))));
    }
    s.labelSpeedMmPerMin = labelSpeed_->mmPerMin();
    s.labelPowerPercent = labelPower_->value();
    s.origin = origin_;
    s.pixelSizeMm = pixelSizeMm_;
    s.overscanMm = operation_.overscanMm;
    return s;
}

int MaterialTestDialog::selectedColumn() const
{
    for (int k = 0; k < tiles_.size(); ++k)
        if (tiles_[k]->isChecked()) return k % cells_;
    return -1;
}

int MaterialTestDialog::selectedRow() const
{
    for (int k = 0; k < tiles_.size(); ++k)
        if (tiles_[k]->isChecked()) return k / cells_;
    return -1;
}

void MaterialTestDialog::scheduleSummary()
{
    if (rebuilding_) return;
    sizeLabel_->setText(tr("Wird berechnet …"));
    summaryTimer_.start();
}

// Größe, G-Code-Umfang und Dauer: Der G-Code wird dafür (ohne ihn zu speichern) einmal erzeugt.
void MaterialTestDialog::updateSummary()
{
    const ld::core::MaterialTestSpec s = spec();
    const ld::core::MaterialTestLayout layout = ld::core::materialTestLayout(s);
    qint64 bytes = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ld::core::GcodeResult result = ld::core::generateMaterialTestGcode(
        s, machine_, [&](const QByteArray& chunk) { bytes += chunk.size(); return true; });
    QApplication::restoreOverrideCursor();
    fits_ = result.ok;
    sizeLabel_->setText(tr("%1 × %2 mm").arg(layout.total.width(), 0, 'f', 0).arg(layout.total.height(), 0, 'f', 0));
    QString warn;
    if (result.ok) {
        linesLabel_->setText(tr("%L1 Zeilen (%2)").arg(result.stats.lines).arg(QLocale().formattedDataSize(result.stats.bytes, 1, QLocale::DataSizeTraditionalFormat)));
        durationLabel_->setText(tr("≈ %1").arg(ld::core::formatDuration(result.stats.seconds)));
    } else {
        linesLabel_->setText(QStringLiteral("–"));
        durationLabel_->setText(QStringLiteral("–"));
        warn = result.error;
    }
    double maxPower = 0.0;
    for (const double p : s.powers) maxPower = std::max(maxPower, p);
    QString text;
    if (result.ok && maxPower > 50.0) {
        text = tr("Hinweis: Die Power geht bis %1 %. Den Test auf Restmaterial brennen, nicht auf dem Werkstück.")
                   .arg(maxPower, 0, 'f', 0);
    }
    summary_->setText(text);
    summary_->setVisible(!text.isEmpty());
    warning_->setText(warn);
    warning_->setVisible(!warn.isEmpty());
    updateButtons();
}

void MaterialTestDialog::setRunState(bool connected, bool idle)
{
    connected_ = connected;
    idle_ = idle;
    updateButtons();
}

void MaterialTestDialog::updateButtons()
{
    const bool canRun = fits_ && connected_ && idle_;
    startButton_->setEnabled(canRun);
    frameButton_->setEnabled(canRun);
    saveGcodeButton_->setEnabled(fits_);
    saveMaterialButton_->setEnabled(selectedColumn() >= 0);
    startButton_->setToolTip(connected_ ? (idle_ ? tr("Brennt den Materialtest.") : tr("Der Laser ist gerade beschäftigt."))
                                         : tr("Der Laser ist nicht verbunden."));
}

}  // namespace ld::app
