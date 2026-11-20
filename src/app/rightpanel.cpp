#include "rightpanel.h"
#include "lockbutton.h"
#include "zerobutton.h"
#include "segmentstyle.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>
#include <initializer_list>
#include <utility>

#include "accordion.h"
#include "appsettings.h"
#include "unitspinboxes.h"
#include "core/engraving.h"
#include "core/imagedocument.h"
#include "core/units.h"

namespace ld::app {

namespace {

// Bereiche der Gravur-Regler (auch beim Zurücksetzen nach der Live-Regelung).
constexpr int kSpeedMin = 100;     // mm/min
constexpr int kPowerMax = 100;     // Prozent

// Zeile "Beschriftung  [Regler]  Wert". Der Wert wird bei Bewegung des Reglers aktualisiert.
// valueFormat enthält %1 als Platzhalter, z. B. "%1 %" oder "%1°". Mit formatter (statt valueFormat)
// bestimmt eine Funktion den Text (für Einheiten); valueLabelOut liefert dann das Wert-Label zurück.
QSlider* addSliderRow(QVBoxLayout* parent, QWidget* owner, const QString& label, int minimum,
                      int maximum, int value, const QString& valueFormat,
                      std::function<QString(int)> formatter = {}, QLabel** valueLabelOut = nullptr)
{
    auto* row = new QHBoxLayout;
    auto* name = new QLabel(label);
    name->setMinimumWidth(70);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    if (!formatter) {
        formatter = [valueFormat](int v) { return valueFormat.arg(v); };
    }
    auto* valueLabel = new QLabel(formatter(value));
    valueLabel->setMinimumWidth(66);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QObject::connect(slider, &QSlider::valueChanged, owner,
                     [valueLabel, formatter](int v) { valueLabel->setText(formatter(v)); });
    row->addWidget(name);
    row->addWidget(slider, 1);
    row->addWidget(valueLabel);
    parent->addLayout(row);
    if (valueLabelOut) {
        *valueLabelOut = valueLabel;
    }
    return slider;
}

// Zahlenfeld mit Einheit (z. B. " mm"), Nachkommastellen und Schrittweite.
QDoubleSpinBox* makeSpin(double minimum, double maximum, double value, int decimals,
                         double step, const QString& suffix)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setValue(value);
    spin->setSuffix(suffix);
    return spin;
}

}  // namespace

RightPanel::RightPanel(ld::core::ImageDocument* document, QWidget* parent)
    : QWidget(parent), document_(document)
{
    // ------------------------------------------------------------ scrollbarer Teil
    auto* content = new QWidget;
    auto* list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);

    // ---------------------------------------------------------------- Datei
    auto* file = new AccordionSection(QStringLiteral("right/file"), tr("Datei"), true);
    {
        auto* row1 = new QHBoxLayout;
        auto* loadImage = new QPushButton(tr("Bild laden …"));
        auto* clear = new QPushButton(tr("Leeren"));
        clear->setToolTip(tr("Leert die Arbeitsfläche und schließt das Projekt (auch eine geladene NC-Datei). "
                             "Bei ungespeicherten Änderungen kommt vorher die Frage, ob das Projekt gespeichert werden soll."));
        connect(loadImage, &QPushButton::clicked, this, &RightPanel::loadImageRequested);
        connect(clear, &QPushButton::clicked, this, &RightPanel::clearRequested);
        row1->addWidget(loadImage);
        row1->addWidget(clear);
        file->contentLayout()->addLayout(row1);

        auto* row2 = new QHBoxLayout;
        auto* editImage = new QPushButton(tr("Bild bearbeiten …"));
        auto* text = new QPushButton(tr("Schrift …"));
        editImageButton_ = editImage;
        connect(editImage, &QPushButton::clicked, this, &RightPanel::editImageRequested);
        connect(text, &QPushButton::clicked, this, &RightPanel::textRequested);
        text->setToolTip(tr("Text eingeben, Schriftart wählen und als Bild einfügen"));
        row2->addWidget(editImage);
        row2->addWidget(text);
        file->contentLayout()->addLayout(row2);
    }
    list->addWidget(file);
    lockable_ << file;
    fileSection_ = file;

    // ---------------------------------------------------------------- Gravur
    auto* engraving = new AccordionSection(QStringLiteral("right/engraving"), tr("Gravur"), true);
    {
        // Speed und Power stehen ganz oben (beim Lauf sind sie die Live-Regler).
        speedSlider_ = addSliderRow(engraving->contentLayout(), this, tr("Speed"), kSpeedMin, 3000, 1200, QString(),
                                    [this](int v) { return ncMode_ ? tr("%1 %").arg(v) : formatSpeed(v); }, &speedValueLabel_);
        powerSlider_ = addSliderRow(engraving->contentLayout(), this, tr("Power"), 0, kPowerMax, 60, tr("%1 %"));

        // Modus: genau einer von drei (Full / Cutting / Linie).
        auto* modeRow = new QHBoxLayout;
        modeRow->setSpacing(0);
        auto* modeGroup = new QButtonGroup(this);
        modeGroup->setExclusive(true);
        fullButton_ = new QPushButton(tr("Full"));
        cuttingButton_ = new QPushButton(tr("Cutting"));
        lineButton_ = new QPushButton(tr("Linie"));
        for (QPushButton* modeButton : {fullButton_, cuttingButton_, lineButton_}) {
            modeButton->setCheckable(true);
            applySegmentStyle(modeButton);
            modeGroup->addButton(modeButton);
            modeButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            modeRow->addWidget(modeButton);
        }
        lineButton_->setChecked(true);
        fullButton_->setToolTip(tr("Full: Zusammenhängende Flächen werden Fläche für Fläche im Zickzack gefüllt.\nEine dünne Linie wird dabei zu einer schmalen Fläche."));
        cuttingButton_->setToolTip(tr("Cutting: Die Umrisse der schwarzen Flächen werden nachgefahren.\nEine dünne Linie wird daher als schmale Doppelkontur ausgegeben."));
        lineButton_->setToolTip(tr("Linie: Rasterzeilen über das ganze Bild"));
        engraving->contentLayout()->addLayout(modeRow);
        const std::pair<QPushButton*, ld::core::EngraveMode> modeButtons[] = {
            {fullButton_, ld::core::EngraveMode::Full},
            {cuttingButton_, ld::core::EngraveMode::Cutting},
            {lineButton_, ld::core::EngraveMode::Line}};
        for (const auto& [button, mode] : modeButtons) {
            connect(button, &QPushButton::clicked, this, [this, mode] {
                if (refreshing_) return;
                // Beim Wechsel zwischen Gravur und Schnitt wechseln auch Speed, Leistung, Durchgänge.
                document_->setOperation(ld::core::switchMode(document_->operation(), mode));
            });
        }

        // Richtung 0°–180° mit Schnellwahl 0°/45°/90°/135° (bei Cutting ausgeblendet).
        angleBox_ = new QWidget;
        auto* angleLayout = new QVBoxLayout(angleBox_);
        angleLayout->setContentsMargins(0, 0, 0, 0);
        angleSlider_ = addSliderRow(angleLayout, this, tr("Richtung"), 0, 180,
                                    0, QStringLiteral("%1°"));
        auto* presetRow = new QHBoxLayout;
        presetRow->setSpacing(0);
        for (const int degrees : {0, 45, 90, 135}) {
            auto* preset = new QPushButton(QStringLiteral("%1°").arg(degrees));
            connect(preset, &QPushButton::clicked, angleSlider_, [this, degrees] { angleSlider_->setValue(degrees); });
            // Ignored: Die Buttons teilen sich die Breite gleichmäßig und machen die Leiste
            // nicht breiter, nur weil Qt für jeden Button eine Mindestbreite vorsieht.
            preset->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            presetRow->addWidget(preset);
        }
        angleLayout->addLayout(presetRow);
        engraving->contentLayout()->addWidget(angleBox_);

        auto* passesRow = new QHBoxLayout;
        passesRow->addWidget(new QLabel(tr("Durchgänge")));
        passesSpin_ = new QSpinBox;
        passesSpin_->setRange(1, 99);
        passesRow->addWidget(passesSpin_);
        passesRow->addStretch();
        engraving->contentLayout()->addLayout(passesRow);

        // Änderungen gehen in die Operation des Dokuments (kein Undo-Schritt, siehe planung.md 3.10).
        auto change = [this](auto modify) {
            if (refreshing_) {
                return;
            }
            ld::core::Operation op = document_->operation();
            modify(op);
            document_->setOperation(op);
        };
        connect(angleSlider_, &QSlider::valueChanged, this, [change](int v) { change([v](auto& op) { op.angleDeg = v; }); });
        // Beim Lauf (liveMode_) ändern die Regler nicht das Dokument, sondern regeln den laufenden Auftrag.
        connect(speedSlider_, &QSlider::valueChanged, this, [this, change](int v) {
            if (liveMode_) { if (!refreshing_) liveTimer_.start(); return; }
            change([v](auto& op) { op.speedMmPerMin = v; });
        });
        connect(powerSlider_, &QSlider::valueChanged, this, [this, change](int v) {
            if (liveMode_) { if (!refreshing_) liveTimer_.start(); return; }
            change([v](auto& op) { op.powerPercent = v; });
        });
        connect(passesSpin_, &QSpinBox::valueChanged, this, [change](int v) { change([v](auto& op) { op.passes = v; }); });
    }
    list->addWidget(engraving);
    // Im Abschnitt Gravur bleiben beim Lauf nur Speed und Leistung bedienbar.
    lockable_ << fullButton_ << cuttingButton_ << lineButton_ << angleBox_ << passesSpin_;

    // ---------------------------------------------------------------- Größe
    auto* size = new AccordionSection(QStringLiteral("right/size"), tr("Größe"), true);
    {
        widthSpin_ = new LengthSpinBox(0.01, 5000.0, 1, 3, 1.0);
        heightSpin_ = new LengthSpinBox(0.01, 5000.0, 1, 3, 1.0);
        rotationSpin_ = makeSpin(-360.0, 360.0, 0.0, 1, 1.0, QStringLiteral(" °"));
        // Erst bei Enter, Fokuswechsel oder Pfeiltasten übernehmen, nicht bei jedem getippten
        // Zeichen: So ergibt eine Eingabe genau einen Undo-Schritt.
        rotationSpin_->setKeyboardTracking(false);
        // Schloss hinter Breite und Höhe: geschlossen = Seitenverhältnis bleibt erhalten, offen = frei.
        lockButton_ = new LockButton;
        auto* grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(2);   // Breite/Höhe und X/Y rücken zusammen; zwischen den Gruppen eine kleine Lücke
        grid->addWidget(new QLabel(tr("Breite")), 0, 0);
        grid->addWidget(widthSpin_, 0, 1);
        grid->addWidget(new QLabel(tr("Höhe")), 1, 0);
        grid->addWidget(heightSpin_, 1, 1);
        grid->addWidget(lockButton_, 0, 2, 2, 1);   // Klammer über beide Zeilen
        grid->setRowMinimumHeight(2, 6);
        grid->addWidget(new QLabel(tr("Drehen")), 3, 0);
        grid->addWidget(rotationSpin_, 3, 1);
        grid->setRowMinimumHeight(4, 6);
        // Lage der Grafik zum Nullpunkt (planung.md 3.24): Position des Job-Ursprungs der Grafik, gezählt wie die Lineale.
        positionXSpin_ = new LengthSpinBox(-5000.0, 5000.0, 1, 3, 1.0);
        positionYSpin_ = new LengthSpinBox(-5000.0, 5000.0, 1, 3, 1.0);
        positionXSpin_->setToolTip(tr("Abstand des Job-Ursprungs der Grafik vom Nullpunkt (Zählrichtung wie die Lineale). "
                                      "Die Grafik lässt sich auch mit der Maus verschieben."));
        positionYSpin_->setToolTip(positionXSpin_->toolTip());
        grid->addWidget(new QLabel(tr("Position X")), 5, 0);
        grid->addWidget(positionXSpin_, 5, 1);
        grid->addWidget(new QLabel(tr("Position Y")), 6, 0);
        grid->addWidget(positionYSpin_, 6, 1);
        // Hinter den Positionsfeldern: Grafik mit ihrem Job-Ursprung auf den Nullpunkt setzen (wie das Schloss, mit Klammer).
        zeroButton_ = new ZeroButton;
        grid->addWidget(zeroButton_, 5, 2, 2, 1);
        connect(zeroButton_, &QToolButton::clicked, this, [this] { document_->setOffsetMm(QPointF()); });
        grid->setColumnStretch(1, 1);
        size->contentLayout()->addLayout(grid);
        connect(lockButton_, &QToolButton::toggled, this, &RightPanel::keepAspectChanged);

        auto* flipRow = new QHBoxLayout;
        flipHorizontalButton_ = new QPushButton(tr("Horizontal spiegeln"));
        flipVerticalButton_ = new QPushButton(tr("Vertikal spiegeln"));
        flipRow->addWidget(flipHorizontalButton_);
        flipRow->addWidget(flipVerticalButton_);
        size->contentLayout()->addLayout(flipRow);

        // Eingaben -> Dokument (jede Änderung wird dort ein Undo-Schritt).
        connect(widthSpin_, &LengthSpinBox::mmChanged, this,
                [this](double mm) { document_->setWidthMm(mm, lockButton_->isChecked()); });
        connect(heightSpin_, &LengthSpinBox::mmChanged, this,
                [this](double mm) { document_->setHeightMm(mm, lockButton_->isChecked()); });
        connect(rotationSpin_, &QDoubleSpinBox::valueChanged, this,
                [this](double degrees) { document_->setRotationDeg(degrees); });
        auto changePosition = [this] {
            // Zahl im Lineal = Vorzeichen * Versatz (siehe jobOriginRulerSign).
            const QPointF sign = ld::core::jobOriginRulerSign(document_->jobOrigin());
            document_->setOffsetMm(QPointF(positionXSpin_->mm() * sign.x(), positionYSpin_->mm() * sign.y()));
        };
        connect(positionXSpin_, &LengthSpinBox::mmChanged, this, changePosition);
        connect(positionYSpin_, &LengthSpinBox::mmChanged, this, changePosition);
        connect(flipHorizontalButton_, &QPushButton::clicked, this,
                [this] { document_->flipHorizontal(); });
        connect(flipVerticalButton_, &QPushButton::clicked, this,
                [this] { document_->flipVertical(); });

        // Die X/Y-Felder gibt es nur bei Lasern mit Endschaltern (Maschinenprofil, M7).
    }
    list->addWidget(size);
    lockable_ << size;

    // ---------------------------------------------------------------- Qualität
    auto* advanced = new AccordionSection(QStringLiteral("right/advanced"), tr("Qualität"), false);
    {
        auto* form = new QFormLayout;
        advancedForm_ = form;
        form->setContentsMargins(0, 0, 0, 0);
        // Vorgabe: 400 dpi -> 0,0635 mm. Der Wert kommt aus der Fachlogik (core).
        pixelSizeSpin_ = new ResolutionSpinBox(0.01, 1.0);
        pixelSizeSpin_->setMm(ld::core::mmPerPixelFromDpi(400.0));
        pixelSizeLabel_ = new QLabel(resolutionLabel());
        form->addRow(pixelSizeLabel_, pixelSizeSpin_);
        // Mit Bild ändert das Feld die Pixelgröße des Bildes (ein Undo-Schritt), ohne Bild ist
        // es die Vorgabe für das nächste geladene Bild.
        connect(pixelSizeSpin_, &ResolutionSpinBox::mmChanged, this, [this](double mm) {
            if (document_->hasImage()) {
                document_->setPixelSizeMm(mm);
            }
        });
        overscanSpin_ = new LengthSpinBox(0.0, 50.0, 1, 3, 0.5);
        overscanSpin_->setMm(1.0);
        overscanSpin_->setToolTip(tr("Anlauf und Auslauf ohne Leistung an beiden Zeilenenden"));
        form->addRow(tr("Überscan"), overscanSpin_);
        toleranceSpin_ = new LengthSpinBox(0.001, 1.0, 3, 5, 0.005);
        toleranceSpin_->setToolTip(tr("Größte Abweichung beim Zerlegen von Kurven in kurze Strecken"));
        form->addRow(tr("Kurvengenauigkeit"), toleranceSpin_);
        advanced->contentLayout()->addLayout(form);
        // Richtwert aus der Beschleunigung des Maschinenprofils (nur, wenn sie bekannt ist).
        overscanHint_ = new QLabel;
        overscanHint_->setWordWrap(true);
        overscanHint_->setEnabled(false);   // gedämpfte Schrift
        overscanHint_->hide();
        advanced->contentLayout()->addWidget(overscanHint_);
        bidirectionalCheck_ = new QCheckBox(tr("Beidseitig gravieren"));
        bidirectionalCheck_->setChecked(true);
        advanced->contentLayout()->addWidget(bidirectionalCheck_);
        connect(overscanSpin_, &LengthSpinBox::mmChanged, this, [this](double mm) {
            if (refreshing_) return;
            ld::core::Operation op = document_->operation();
            op.overscanMm = mm;
            document_->setOperation(op);
        });
        connect(toleranceSpin_, &LengthSpinBox::mmChanged, this, [this](double mm) {
            if (refreshing_) return;
            ld::core::Operation op = document_->operation();
            op.toleranceMm = mm;
            document_->setOperation(op);
        });
        connect(bidirectionalCheck_, &QCheckBox::toggled, this, [this](bool on) {
            if (refreshing_) return;
            ld::core::Operation op = document_->operation();
            op.bidirectional = on;
            document_->setOperation(op);
        });
    }
    list->addWidget(advanced);
    lockable_ << advanced;

    // ---------------------------------------------------------------- Rahmen
    auto* frame =
        new AccordionSection(QStringLiteral("right/frame"), tr("Rahmen"), false);
    {
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        frameSpeedSpin_ = new SpeedSpinBox(100.0, 5000.0);
        frameSpeedSpin_->setMmPerMin(3000.0);
        form->addRow(tr("Speed"), frameSpeedSpin_);
        framePowerSpin_ = makeSpin(0.0, 100.0, 0.0, 0, 1.0, tr(" %"));
        framePowerSpin_->setToolTip(tr("0 %: Der Laser bleibt beim Abfahren des Rahmens aus"));
        form->addRow(tr("Power"), framePowerSpin_);
        frame->contentLayout()->addLayout(form);
    }
    list->addWidget(frame);
    frameSection_ = frame;

    list->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    // Mindestbreite = Breite, die der Inhalt braucht (plus senkrechte Bildlaufleiste).
    // Ohne diese Zeile darf die Leiste schmaler werden als ihr Inhalt, und weil die
    // waagerechte Bildlaufleiste aus ist, würde der Überstand einfach abgeschnitten.
    scroll->setMinimumWidth(content->minimumSizeHint().width()
                            + scroll->verticalScrollBar()->sizeHint().width() + 4);

    // ------------------------------------------------------------ fester Fuß (scrollt nicht)
    auto* footer = new QFrame;
    footer->setFrameShape(QFrame::HLine);  // dünne Trennlinie oben
    auto* footerBox = new QWidget;
    auto* footerLayout = new QVBoxLayout(footerBox);
    footerLayout->setContentsMargins(10, 8, 10, 8);

    auto* buttonRow = new QHBoxLayout;
    auto* start = new QPushButton(tr("Start"));
    auto* pause = new QPushButton(tr("Pause"));
    auto* testSize = new QPushButton(tr("Rahmen"));
    testSizeButton_ = testSize;
    startButton_ = start;
    pauseButton_ = pause;
    for (QPushButton* button : {start, pause, testSize}) {
        button->setEnabled(false);  // M4: erst mit Verbindung und erzeugtem G-Code
        button->setMinimumHeight(34);
        buttonRow->addWidget(button);
    }
    // Start ist blau (Hervorhebungsfarbe des Designs, seit 2026-11-18; vorher rot).
    start->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(highlight); color: palette(highlighted-text); font-weight: 600; border-radius: 3px; }"
        "QPushButton:disabled { background: palette(mid); color: palette(button); }"));
    footerLayout->addLayout(buttonRow);

    // Live-Regelung (M8): Beim Lauf wirken die Regler "Speed" und "Leistung" aus dem Abschnitt Gravur
    // als Prozent-Regler auf den laufenden Auftrag. Sie melden erst nach einer kurzen Pause (nicht bei
    // jedem Zwischenwert), weil GRBL nur Schritte kennt.
    liveTimer_.setSingleShot(true);
    liveTimer_.setInterval(250);
    connect(&liveTimer_, &QTimer::timeout, this, [this] {
        emit liveSpeedChanged(livePercent(speedSlider_->value(), liveBaseSpeed_));
        emit livePowerChanged(livePercent(powerSlider_->value(), liveBasePower_));
    });

    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, [this] { updateSpeedLabel(); updateOverscanHint(); pixelSizeLabel_->setText(resolutionLabel()); });
    connect(document_, &ld::core::ImageDocument::transformChanged, this,
            &RightPanel::refreshFromDocument);
    connect(document_, &ld::core::ImageDocument::engravingChanged, this,
            &RightPanel::refreshFromDocument);
    refreshFromDocument();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll, 1);
    outer->addWidget(footer);
    outer->addWidget(footerBox);
}


int RightPanel::livePercent(int value, int base)
{
    return base > 0 ? static_cast<int>(std::lround(value * 100.0 / base)) : 100;
}

void RightPanel::beginLiveControl()
{
    if (ncMode_) {   // bei einer NC-Datei sind 100 % immer "wie in der Datei"
        const bool before = refreshing_;
        refreshing_ = true;
        speedSlider_->setValue(100);
        powerSlider_->setValue(100);
        refreshing_ = before;
        return;
    }
    // Die aktuellen Werte sind "100 %". Speed und Leistung dürfen auf 10 bis 200 % (Grenzen von GRBL);
    // die Leistung geht aber nie über 100 % der Maschine, GRBL kappt sie selbst bei $30.
    liveBaseSpeed_ = speedSlider_->value();
    liveBasePower_ = powerSlider_->value();
    liveMode_ = true;
    const bool before = refreshing_;
    refreshing_ = true;
    speedSlider_->setRange(std::max(1, liveBaseSpeed_ / 10), liveBaseSpeed_ * 2);
    powerSlider_->setRange(liveBasePower_ > 0 ? std::max(1, liveBasePower_ / 10) : 0, std::min(kPowerMax, liveBasePower_ * 2));
    speedSlider_->setValue(liveBaseSpeed_);
    powerSlider_->setValue(liveBasePower_);
    refreshing_ = before;
}

void RightPanel::endLiveControl()
{
    if (ncMode_) {   // die Regler bleiben im NC-Modus Prozent-Regler
        const bool before = refreshing_;
        refreshing_ = true;
        speedSlider_->setValue(100);
        powerSlider_->setValue(100);
        refreshing_ = before;
        return;
    }
    if (!liveMode_) return;
    liveMode_ = false;
    liveTimer_.stop();
    // Ursprüngliche Bereiche und die Werte aus dem Dokument zurück (das Dokument wurde nie verändert).
    const bool before = refreshing_;
    refreshing_ = true;
    speedSlider_->setRange(kSpeedMin, speedMax());
    powerSlider_->setRange(0, kPowerMax);
    speedSlider_->setValue(static_cast<int>(document_->operation().speedMmPerMin));
    powerSlider_->setValue(static_cast<int>(document_->operation().powerPercent));
    refreshing_ = before;
}

bool RightPanel::keepAspect() const { return lockButton_->isChecked(); }

void RightPanel::setEditingLocked(bool locked)
{
    locked_ = locked;
    applyLockStates();
}

// Gesperrt ist, was ein laufender Auftrag nicht ändern darf; im NC-Modus zusätzlich alles zum Bild (nur der Abschnitt
// Datei bleibt bedienbar, damit sich ein Bild laden oder die NC-Datei schließen lässt).
void RightPanel::applyLockStates()
{
    for (QWidget* widget : lockable_) {
        const bool enabled = widget == fileSection_ ? !locked_ : !(locked_ || ncMode_);
        widget->setEnabled(enabled);
    }
    editImageButton_->setEnabled(document_->hasImage() && !ncMode_);
    // Während ein Auftrag brennt, sind auch die Rahmen-Felder gesperrt (im NC-Modus bleiben sie bedienbar).
    if (frameSection_) frameSection_->setEnabled(!locked_);
}

void RightPanel::setNcMode(bool on)
{
    ncMode_ = on;
    liveTimer_.stop();
    const bool before = refreshing_;
    refreshing_ = true;
    if (on) {
        liveMode_ = true;   // die Regler gehen nie ins Dokument, sondern immer als Prozent an GRBL
        liveBaseSpeed_ = liveBasePower_ = 100;
        speedSlider_->setRange(10, 200);
        powerSlider_->setRange(10, 200);
        speedSlider_->setValue(100);
        powerSlider_->setValue(100);
    } else {
        liveMode_ = false;
        speedSlider_->setRange(kSpeedMin, speedMax());
        powerSlider_->setRange(0, kPowerMax);
        speedSlider_->setValue(static_cast<int>(document_->operation().speedMmPerMin));
        powerSlider_->setValue(static_cast<int>(document_->operation().powerPercent));
    }
    refreshing_ = before;
    updateSpeedLabel();
    applyLockStates();
}

int RightPanel::speedSliderValue() const { return speedSlider_->value(); }
int RightPanel::speedSliderMin() const { return speedSlider_->minimum(); }
int RightPanel::speedSliderMax() const { return speedSlider_->maximum(); }
int RightPanel::powerSliderValue() const { return powerSlider_->value(); }
int RightPanel::powerSliderMin() const { return powerSlider_->minimum(); }
int RightPanel::powerSliderMax() const { return powerSlider_->maximum(); }

void RightPanel::setLiveValues(int speedPercent, int powerPercent)
{
    // Abgleich mit dem Wert, den GRBL tatsächlich hat (zum Beispiel nach Abbruch). Stimmt der Regler
    // schon (auf Prozent gerundet), bleibt er stehen, damit er unter der Maus nicht springt.
    if (!liveMode_) return;
    const bool before = refreshing_;
    refreshing_ = true;
    if (livePercent(speedSlider_->value(), liveBaseSpeed_) != speedPercent) {
        speedSlider_->setValue(static_cast<int>(std::lround(liveBaseSpeed_ * speedPercent / 100.0)));
    }
    if (livePercent(powerSlider_->value(), liveBasePower_) != powerPercent) {
        powerSlider_->setValue(static_cast<int>(std::lround(liveBasePower_ * powerPercent / 100.0)));
    }
    refreshing_ = before;
}

double RightPanel::frameSpeedMmPerMin() const { return frameSpeedSpin_->mmPerMin(); }
double RightPanel::framePowerPercent() const { return framePowerSpin_->value(); }

void RightPanel::setFrameValues(double speedMmPerMin, double powerPercent)
{
    frameSpeedSpin_->setMmPerMin(speedMmPerMin);
    framePowerSpin_->setValue(powerPercent);
}

double RightPanel::pixelSizeMm() const
{
    return pixelSizeSpin_->mm();
}

void RightPanel::setDefaultPixelSizeMm(double mm)
{
    if (!document_->hasImage() && mm > 0.0) {
        pixelSizeSpin_->setMm(mm);
    }
}

int RightPanel::speedMax() const
{
    return std::max(kSpeedMin + 1, static_cast<int>(std::lround(machine_.maxSpeedMmPerMin)));
}

void RightPanel::setMachine(const ld::core::MachineSetup& machine)
{
    machine_ = machine;
    // Obere Grenze des Speed-Reglers (nicht beim Lauf und nicht im NC-Modus: dort sind es Prozent).
    if (!liveMode_ && !ncMode_) {
        const bool before = refreshing_;
        refreshing_ = true;
        speedSlider_->setRange(kSpeedMin, speedMax());
        refreshing_ = before;
        enforceSpeedLimit();
    }
    {
        frameSpeedSpin_->setMaxMmPerMin(std::max<double>(kSpeedMin + 1, machine_.maxSpeedMmPerMin));   // auch der Rahmen nicht schneller
    }
    updateOverscanHint();
}

// Ist die Speed der Operation höher als die Grenze des Maschinenprofils, wird sie auf die Grenze gesetzt.
void RightPanel::enforceSpeedLimit()
{
    if (ncMode_ || liveMode_) return;
    const ld::core::Operation& op = document_->operation();
    if (op.speedMmPerMin > speedMax() + 1e-6) {
        ld::core::Operation limited = op;
        limited.speedMmPerMin = speedMax();
        document_->setOperation(limited);
        emit speedLimited(speedMax());
    }
}

void RightPanel::updateOverscanHint()
{
    const ld::core::Operation& op = document_->operation();
    const double mm = ld::core::overscanRecommendationMm(machine_, op.angleDeg, op.speedMmPerMin);
    overscanHint_->setVisible(mm > 0.0 && op.mode != ld::core::EngraveMode::Cutting);
    if (mm > 0.0) {
        overscanHint_->setText(tr("Richtwert: %1 bei %2").arg(formatLength(mm, 1), formatSpeed(op.speedMmPerMin)));
    }
}

void RightPanel::updateSpeedLabel()
{
    speedValueLabel_->setText(ncMode_ ? tr("%1 %").arg(speedSlider_->value()) : formatSpeed(speedSlider_->value()));
}

void RightPanel::updateModeVisibility()
{
    using ld::core::EngraveMode;
    const EngraveMode mode = document_->operation().mode;
    const bool cutting = mode == EngraveMode::Cutting;
    const bool full = mode == EngraveMode::Full;
    angleBox_->setVisible(!cutting);
    advancedForm_->setRowVisible(overscanSpin_, !cutting);
    advancedForm_->setRowVisible(toleranceSpin_, cutting || full);
    // Im Cutting zählen Konturen, nicht Pixelzeilen: Die Auflösung wird dort nicht gebraucht und ist ausgeblendet.
    advancedForm_->setRowVisible(pixelSizeSpin_, !cutting);
    bidirectionalCheck_->setVisible(!cutting);
    if (cutting) {
        overscanHint_->hide();
    }
}

void RightPanel::refreshFromDocument()
{
    const bool hasImage = document_->hasImage();
    const ld::core::ObjectTransform& t = document_->transform();

    // Werte setzen, ohne dass die Felder ihr valueChanged-Signal senden (sonst käme eine
    // Endlosschleife: Dokument -> Feld -> Dokument).
    widthSpin_->setMm(t.widthMm);
    heightSpin_->setMm(t.heightMm);
    {
        const QSignalBlocker blocker(rotationSpin_);
        rotationSpin_->setValue(t.rotationDeg);
    }
    {
        const QPointF sign = ld::core::jobOriginRulerSign(document_->jobOrigin());
        positionXSpin_->setMm(sign.x() * t.offsetXMm);
        positionYSpin_->setMm(sign.y() * t.offsetYMm);
    }

    // Gravur-Felder aus der Operation füllen (ohne dass sie das Dokument erneut ändern).
    {
        refreshing_ = true;
        const ld::core::Operation& op = document_->operation();
        angleSlider_->setValue(static_cast<int>(op.angleDeg));
        if (!ncMode_) {   // im NC-Modus sind die Regler Prozent-Regler
            speedSlider_->setValue(static_cast<int>(op.speedMmPerMin));
            powerSlider_->setValue(static_cast<int>(op.powerPercent));
        }
        passesSpin_->setValue(op.passes);
        const QSignalBlocker b2(bidirectionalCheck_);
        overscanSpin_->setMm(op.overscanMm);
        bidirectionalCheck_->setChecked(op.bidirectional);
        toleranceSpin_->setMm(op.toleranceMm);
        fullButton_->setChecked(op.mode == ld::core::EngraveMode::Full);
        cuttingButton_->setChecked(op.mode == ld::core::EngraveMode::Cutting);
        lineButton_->setChecked(op.mode == ld::core::EngraveMode::Line);
        refreshing_ = false;
        updateOverscanHint();
        updateModeVisibility();
        enforceSpeedLimit();
    }

    // Pixelgröße des Bildes anzeigen (ohne Bild bleibt die Vorgabe stehen).
    if (hasImage) {
        pixelSizeSpin_->setMm(document_->settings().pixelSizeMm);
    }
    editImageButton_->setEnabled(hasImage && !ncMode_);

    // Ohne Bild gibt es nichts zu ändern.
    for (QWidget* widget : std::initializer_list<QWidget*>{widthSpin_, heightSpin_, rotationSpin_, positionXSpin_, positionYSpin_, zeroButton_,
                                                           lockButton_, flipHorizontalButton_,
                                                           flipVerticalButton_}) {
        widget->setEnabled(hasImage);
    }
}

}  // namespace ld::app

