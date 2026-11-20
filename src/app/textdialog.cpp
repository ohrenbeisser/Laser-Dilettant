#include "textdialog.h"
#include "helpwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "appsettings.h"
#include "segmentstyle.h"
#include "textpreview.h"
#include "unitspinboxes.h"

namespace ld::app {

using ld::core::TextAlign;
using ld::core::TextSpec;

namespace {

constexpr int kLeftWidth = 330;   // Breite der Eingabespalte

// Kleine Überschrift über einer Gruppe von Feldern.
QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

// Zeile "Regler + Zahlenfeld", beide folgen einander.
void linkSliderAndSpin(QSlider* slider, QSpinBox* spin)
{
    QObject::connect(slider, &QSlider::valueChanged, spin, [spin](int v) {
        const QSignalBlocker b(spin);
        spin->setValue(v);
    });
    QObject::connect(spin, &QSpinBox::valueChanged, slider, [slider](int v) {
        const QSignalBlocker b(slider);
        slider->setValue(v);
    });
}

}  // namespace

TextDialog::TextDialog(const ld::core::MachineSetup& machine, const std::optional<TextSpec>& initial, QWidget* parent)
    : QDialog(parent), machine_(machine), editing_(initial.has_value())
{
    setWindowTitle(editing_ ? tr("Text bearbeiten") : tr("Text einfügen"));
    attachHelp(this, ld::core::helptopic::kText);   // F1 und Knopf "Hilfe" (Handbuch)
    // Wie der Dialog "Bild bearbeiten": Titelleiste mit Minimieren und Maximieren, in der Größe veränderbar.
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowCloseButtonHint);
    setSizeGripEnabled(true);
    setMinimumSize(820, 520);
    resize(1000, 640);
    restoreGeometry(QSettings().value(QStringLiteral("textDialog/geometry")).toByteArray());

    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(90);
    connect(&updateTimer_, &QTimer::timeout, this, &TextDialog::update);

    buildUi();

    // Startwerte: vorhandener Text oder die zuletzt benutzten Einstellungen.
    TextSpec start;
    if (initial) {
        start = *initial;
    } else {
        QSettings s;
        start.fontFamily = s.value(QStringLiteral("textDialog/font"), QApplication::font().family()).toString();
        start.bold = s.value(QStringLiteral("textDialog/bold"), false).toBool();
        start.italic = s.value(QStringLiteral("textDialog/italic"), false).toBool();
        start.heightMm = s.value(QStringLiteral("textDialog/heightMm"), 10.0).toDouble();
        start.letterSpacingPercent = s.value(QStringLiteral("textDialog/letterSpacing"), 0.0).toDouble();
        start.lineSpacing = s.value(QStringLiteral("textDialog/lineSpacing"), 1.0).toDouble();
        start.align = static_cast<TextAlign>(s.value(QStringLiteral("textDialog/align"), 0).toInt());
    }
    if (start.fontFamily.isEmpty()) {
        start.fontFamily = QApplication::font().family();
    }
    applySpec(ld::core::sanitized(start));
    update();
    textEdit_->setFocus();
    textEdit_->selectAll();
}

void TextDialog::buildUi()
{
    auto* main = new QVBoxLayout(this);
    main->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    main->addLayout(body, 1);

    // ------------------------------------------------------------------ links: Eingaben
    auto* left = new QWidget;
    left->setFixedWidth(kLeftWidth);
    auto* form = new QVBoxLayout(left);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    form->addWidget(heading(tr("Text"), left));
    textEdit_ = new QPlainTextEdit;
    textEdit_->setPlaceholderText(tr("Text eingeben …\nEin Zeilenumbruch beginnt eine neue Zeile."));
    textEdit_->setTabChangesFocus(true);
    textEdit_->setMinimumHeight(110);
    {
        QFont f = textEdit_->font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        textEdit_->setFont(f);
    }
    form->addWidget(textEdit_, 1);
    connect(textEdit_, &QPlainTextEdit::textChanged, this, &TextDialog::scheduleUpdate);

    form->addSpacing(6);
    form->addWidget(heading(tr("Schriftart"), left));
    auto* fontRow = new QHBoxLayout;
    fontRow->setSpacing(4);
    fontCombo_ = new QFontComboBox;
    fontCombo_->setFontFilters(QFontComboBox::ScalableFonts);
    fontCombo_->setToolTip(tr("Alle auf diesem Rechner installierten Schriften. Zum Suchen einfach tippen."));
    fontCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    fontCombo_->setMinimumContentsLength(10);
    boldButton_ = new QPushButton(tr("F"));
    boldButton_->setToolTip(tr("Fett"));
    italicButton_ = new QPushButton(tr("K"));
    italicButton_->setToolTip(tr("Kursiv"));
    for (QPushButton* b : {boldButton_, italicButton_}) {
        b->setCheckable(true);
        b->setFixedWidth(34);
        applySegmentStyle(b);
    }
    {
        QFont f = boldButton_->font();
        f.setBold(true);
        boldButton_->setFont(f);
        QFont g = italicButton_->font();
        g.setItalic(true);
        italicButton_->setFont(g);
    }
    fontRow->addWidget(fontCombo_, 1);
    fontRow->addWidget(boldButton_);
    fontRow->addWidget(italicButton_);
    form->addLayout(fontRow);
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this, &TextDialog::scheduleUpdate);
    connect(boldButton_, &QPushButton::toggled, this, &TextDialog::scheduleUpdate);
    connect(italicButton_, &QPushButton::toggled, this, &TextDialog::scheduleUpdate);

    form->addSpacing(6);
    form->addWidget(heading(tr("Größe und Anordnung"), left));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel(tr("Zeichenhöhe")), row, 0);
    heightSpin_ = new LengthSpinBox(ld::core::kTextMinHeightMm, ld::core::kTextMaxHeightMm, 1, 2, 0.5);
    heightSpin_->setToolTip(tr("Höhe der Großbuchstaben (Versalhöhe). Buchstaben mit Unter- oder Oberlängen "
                               "ragen darüber hinaus."));
    grid->addWidget(heightSpin_, row, 1, 1, 2);
    connect(heightSpin_, &LengthSpinBox::mmChanged, this, &TextDialog::scheduleUpdate);
    ++row;

    grid->addWidget(new QLabel(tr("Ausrichtung")), row, 0);
    {
        auto* box = new QWidget;
        auto* l = new QHBoxLayout(box);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        alignGroup_ = new QButtonGroup(this);
        alignGroup_->setExclusive(true);
        const QList<QPair<QString, int>> entries = {{tr("Links"), 0}, {tr("Mitte"), 1}, {tr("Rechts"), 2}};
        for (const auto& [text, id] : entries) {
            auto* b = new QPushButton(text);
            b->setCheckable(true);
            applySegmentStyle(b);
            alignGroup_->addButton(b, id);
            l->addWidget(b, 1);
        }
        alignGroup_->button(0)->setChecked(true);
        box->setToolTip(tr("Ausrichtung der Zeilen zueinander (wirkt bei mehr als einer Zeile)."));
        grid->addWidget(box, row, 1, 1, 2);
    }
    connect(alignGroup_, &QButtonGroup::idClicked, this, &TextDialog::scheduleUpdate);
    ++row;

    grid->addWidget(new QLabel(tr("Buchstabenabstand")), row, 0);
    letterSlider_ = new QSlider(Qt::Horizontal);
    letterSlider_->setRange(static_cast<int>(ld::core::kTextMinSpacingPercent), static_cast<int>(ld::core::kTextMaxSpacingPercent));
    letterSpin_ = new QSpinBox;
    letterSpin_->setRange(letterSlider_->minimum(), letterSlider_->maximum());
    letterSpin_->setSuffix(QStringLiteral(" %"));
    letterSpin_->setKeyboardTracking(false);
    letterSpin_->setToolTip(tr("Zusätzlicher Abstand zwischen den Buchstaben, in Prozent der Zeichenhöhe. "
                               "0 % = Abstand der Schrift."));
    letterSlider_->setToolTip(letterSpin_->toolTip());
    grid->addWidget(letterSlider_, row, 1);
    grid->addWidget(letterSpin_, row, 2);
    linkSliderAndSpin(letterSlider_, letterSpin_);
    connect(letterSlider_, &QSlider::valueChanged, this, &TextDialog::scheduleUpdate);
    connect(letterSpin_, &QSpinBox::valueChanged, this, &TextDialog::scheduleUpdate);
    ++row;

    grid->addWidget(new QLabel(tr("Zeilenabstand")), row, 0);
    lineSlider_ = new QSlider(Qt::Horizontal);
    lineSlider_->setRange(static_cast<int>(ld::core::kTextMinLineSpacing * 100), static_cast<int>(ld::core::kTextMaxLineSpacing * 100));
    lineSpin_ = new QSpinBox;
    lineSpin_->setRange(lineSlider_->minimum(), lineSlider_->maximum());
    lineSpin_->setSuffix(QStringLiteral(" %"));
    lineSpin_->setSingleStep(5);
    lineSpin_->setKeyboardTracking(false);
    lineSpin_->setToolTip(tr("Abstand der Zeilen im Verhältnis zum Zeilenabstand der Schrift (100 % = normal)."));
    lineSlider_->setToolTip(lineSpin_->toolTip());
    grid->addWidget(lineSlider_, row, 1);
    grid->addWidget(lineSpin_, row, 2);
    linkSliderAndSpin(lineSlider_, lineSpin_);
    connect(lineSlider_, &QSlider::valueChanged, this, &TextDialog::scheduleUpdate);
    connect(lineSpin_, &QSpinBox::valueChanged, this, &TextDialog::scheduleUpdate);
    form->addLayout(grid);

    form->addSpacing(8);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    form->addWidget(line);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Ergebnis")));
    sizeLabel_ = new QLabel;
    {
        QFont f = sizeLabel_->font();
        f.setBold(true);
        sizeLabel_->setFont(f);
    }
    sizeRow->addWidget(sizeLabel_, 1, Qt::AlignRight);
    form->addLayout(sizeRow);
    warningLabel_ = new QLabel;
    warningLabel_->setWordWrap(true);
    warningLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    warningLabel_->hide();
    form->addWidget(warningLabel_);
    body->addWidget(left);

    // ------------------------------------------------------------------ rechts: Vorschau
    auto* right = new QVBoxLayout;
    right->setSpacing(6);
    preview_ = new TextPreview;
    preview_->setMinimumSize(380, 300);
    preview_->setFrameShape(QFrame::StyledPanel);
    right->addWidget(preview_, 1);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    auto* zoomOut = new QPushButton(QStringLiteral("−"));
    auto* zoomIn = new QPushButton(QStringLiteral("+"));
    auto* fit = new QPushButton(tr("Einpassen"));
    zoomOut->setToolTip(tr("Verkleinern (Strg+−)"));
    zoomIn->setToolTip(tr("Vergrößern (Strg++)"));
    fit->setToolTip(tr("Text in die Vorschau einpassen"));
    zoomOut->setFixedWidth(34);
    zoomIn->setFixedWidth(34);
    connect(zoomOut, &QPushButton::clicked, preview_, &TextPreview::zoomOut);
    connect(zoomIn, &QPushButton::clicked, preview_, &TextPreview::zoomIn);
    connect(fit, &QPushButton::clicked, preview_, &TextPreview::fit);
    zoomLabel_ = new QLabel;
    zoomLabel_->setEnabled(false);
    positionLabel_ = new QLabel;
    positionLabel_->setMinimumWidth(170);
    positionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bar->addWidget(zoomOut);
    bar->addWidget(zoomIn);
    bar->addWidget(fit);
    bar->addSpacing(8);
    bar->addWidget(zoomLabel_);
    bar->addStretch(1);
    bar->addWidget(positionLabel_);
    right->addLayout(bar);
    body->addLayout(right, 1);

    connect(preview_, &TextPreview::mouseMoved, this, [this](bool inside, const QPointF& mm) {
        positionLabel_->setText(inside ? tr("X %1   Y %2").arg(formatLength(mm.x(), 1), formatLength(mm.y(), 1))
                                       : QString());
    });
    connect(preview_, &TextPreview::zoomChanged, this, [this](double ppm) {
        zoomLabel_->setText(tr("Zoom %1 %").arg(static_cast<int>(std::round(ppm / 96.0 * 25.4 * 100.0))));
    });
    new QShortcut(QKeySequence::ZoomIn, this, [this] { preview_->zoomIn(); });
    new QShortcut(QKeySequence::ZoomOut, this, [this] { preview_->zoomOut(); });

    // ------------------------------------------------------------------ unten: Knöpfe
    auto* buttons = new QDialogButtonBox;
    okButton_ = buttons->addButton(tr("Übernehmen"), QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    okButton_->setDefault(true);
    okButton_->setAutoDefault(true);
    okButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(highlight); color: palette(highlighted-text); font-weight: 600;"
        " border-radius: 3px; padding: 5px 18px; }"
        "QPushButton:disabled { background: palette(mid); color: palette(placeholder-text); }"));
    cancel->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* hint = new QLabel(editing_ ? tr("Übernehmen ersetzt den bisherigen Text auf der Arbeitsfläche.")
                                     : tr("Übernehmen ersetzt das Bild auf der Arbeitsfläche."));
    hint->setEnabled(false);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(hint, 1);
    bottom->addWidget(buttons);
    main->addLayout(bottom);
    // Enter schreibt im Textfeld einen Zeilenumbruch; Strg+Enter übernimmt.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, [this] { accept(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, [this] { accept(); });
}

void TextDialog::applySpec(const TextSpec& spec)
{
    loading_ = true;
    {
        const QSignalBlocker b1(textEdit_), b2(fontCombo_), b3(boldButton_), b4(italicButton_), b5(letterSlider_),
            b6(letterSpin_), b7(lineSlider_), b8(lineSpin_);
        textEdit_->setPlainText(spec.text);
        fontCombo_->setCurrentFont(QFont(spec.fontFamily));
        boldButton_->setChecked(spec.bold);
        italicButton_->setChecked(spec.italic);
        letterSlider_->setValue(static_cast<int>(std::lround(spec.letterSpacingPercent)));
        letterSpin_->setValue(letterSlider_->value());
        lineSlider_->setValue(static_cast<int>(std::lround(spec.lineSpacing * 100.0)));
        lineSpin_->setValue(lineSlider_->value());
    }
    heightSpin_->setMm(spec.heightMm);
    if (auto* b = alignGroup_->button(static_cast<int>(spec.align))) {
        b->setChecked(true);
    }
    loading_ = false;
}

TextSpec TextDialog::spec() const
{
    TextSpec s;
    s.text = textEdit_->toPlainText();
    s.fontFamily = fontCombo_->currentFont().family();
    s.bold = boldButton_->isChecked();
    s.italic = italicButton_->isChecked();
    s.heightMm = heightSpin_->mm();
    s.letterSpacingPercent = letterSpin_->value();
    s.lineSpacing = lineSpin_->value() / 100.0;
    s.align = static_cast<TextAlign>(std::max(0, alignGroup_->checkedId()));
    return ld::core::sanitized(s);
}

void TextDialog::scheduleUpdate()
{
    if (!loading_) {
        updateTimer_.start();
    }
}

void TextDialog::update()
{
    shape_ = ld::core::layoutText(spec());
    preview_->setShape(shape_, tr("Gib links einen Text ein.\nHier erscheint die Vorschau."));
    updateInfo();
}

void TextDialog::updateInfo()
{
    const bool has = !shape_.isEmpty();
    okButton_->setEnabled(has);
    if (!has) {
        sizeLabel_->setText(QStringLiteral("–"));
        warningLabel_->hide();
        return;
    }
    sizeLabel_->setText(tr("%1 × %2").arg(formatLength(shape_.sizeMm.width(), 1), formatLength(shape_.sizeMm.height(), 1)));
    const QSizeF area = ld::core::workAreaInOperatorView(machine_);
    if (shape_.sizeMm.width() > area.width() || shape_.sizeMm.height() > area.height()) {
        warningLabel_->setText(tr("Der Text ist größer als der Arbeitsbereich (%1 × %2).")
                                   .arg(formatLength(area.width(), 0), formatLength(area.height(), 0)));
        warningLabel_->show();
    } else {
        warningLabel_->hide();
    }
}

void TextDialog::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("textDialog/geometry"), saveGeometry());
    const TextSpec t = spec();
    s.setValue(QStringLiteral("textDialog/font"), t.fontFamily);
    s.setValue(QStringLiteral("textDialog/bold"), t.bold);
    s.setValue(QStringLiteral("textDialog/italic"), t.italic);
    s.setValue(QStringLiteral("textDialog/heightMm"), t.heightMm);
    s.setValue(QStringLiteral("textDialog/letterSpacing"), t.letterSpacingPercent);
    s.setValue(QStringLiteral("textDialog/lineSpacing"), t.lineSpacing);
    s.setValue(QStringLiteral("textDialog/align"), static_cast<int>(t.align));
}

void TextDialog::accept()
{
    // Eine noch wartende Änderung (Tippen und sofort Strg+Enter) erst einrechnen.
    if (updateTimer_.isActive()) {
        updateTimer_.stop();
        update();
    }
    if (shape_.isEmpty()) {
        return;   // ohne Text gibt es nichts zu übernehmen
    }
    QDialog::accept();
}

void TextDialog::done(int result)
{
    // Größe, Lage und Einstellungen für das nächste Öffnen merken (auch bei Abbrechen).
    saveSettings();
    QDialog::done(result);
}

}  // namespace ld::app
