#include "calibrationdialog.h"
#include "helpwindow.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>
#include <cmath>

#include "appsettings.h"
#include "textpreview.h"
#include "unitspinboxes.h"

namespace ld::app {

using ld::core::CalibrationSpec;

namespace {

constexpr int kLeftWidth = 500;

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

QString factorText(double factor)
{
    return QLocale().toString(factor, 'f', 4);
}

// Messfeld: 0 = nicht gemessen ("–" angezeigt).
LengthSpinBox* measuredBox()
{
    auto* box = new LengthSpinBox(0.0, 1000.0, 2, 3, 0.1);
    box->setSpecialValueText(QStringLiteral("–"));
    return box;
}

QString deviationText(double nominalMm, double measuredMm)
{
    if (measuredMm <= 0.0) return QString();
    const double diff = measuredMm - nominalMm;
    const double percent = diff / nominalMm * 100.0;
    return QCoreApplication::translate("ld::app::CalibrationDialog", "%1 mm (%2 %)")
        .arg(QLocale().toString(diff, 'f', 2), QLocale().toString(percent, 'f', 2));
}

}  // namespace

CalibrationDialog::CalibrationDialog(const ld::core::MachineSetup& machine, const std::optional<CalibrationSpec>& initial,
                                     QWidget* parent)
    : QDialog(parent), machine_(machine)
{
    setWindowTitle(tr("Kalibrierung"));
    attachHelp(this, ld::core::helptopic::kCalibration);   // F1 und Knopf "Hilfe" (Handbuch)
    setModal(false);   // die Arbeitsfläche dahinter und der Not-Aus bleiben bedienbar
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowCloseButtonHint);
    setSizeGripEnabled(true);
    setMinimumSize(900, 600);
    resize(1080, 700);
    restoreGeometry(QSettings().value(QStringLiteral("calibrationDialog/geometry")).toByteArray());

    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(90);
    connect(&updateTimer_, &QTimer::timeout, this, &CalibrationDialog::updatePreview);

    buildUi();

    CalibrationSpec start;
    if (initial) {
        start = *initial;
    } else {
        QSettings s;
        start.lengthXMm = s.value(QStringLiteral("calibrationDialog/lengthX"), 100.0).toDouble();
        start.lengthYMm = s.value(QStringLiteral("calibrationDialog/lengthY"), 100.0).toDouble();
        start.circleDiameterMm = s.value(QStringLiteral("calibrationDialog/diameter"), 100.0).toDouble();
    }
    start = ld::core::sanitized(start);
    lengthX_->setMm(start.lengthXMm);
    lengthY_->setMm(start.lengthYMm);
    diameter_->setMm(start.circleDiameterMm);
    updateLabels();
    updatePreview();
}

void CalibrationDialog::buildUi()
{
    auto* main = new QVBoxLayout(this);
    main->setSpacing(10);
    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    main->addLayout(body, 1);

    // ------------------------------------------------------------------ links
    auto* left = new QWidget;
    left->setFixedWidth(kLeftWidth);
    auto* form = new QVBoxLayout(left);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);

    // ---- Gruppe 1: Messgrafik
    auto* graphicGroup = new QGroupBox(tr("1  Messgrafik"));
    auto* graphicLayout = new QVBoxLayout(graphicGroup);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);
    grid->addWidget(new QLabel(tr("Achse waagerecht")), 0, 0);
    grid->addWidget(new QLabel(tr("Achse senkrecht")), 1, 0);
    grid->addWidget(new QLabel(tr("Kreis-Durchmesser")), 2, 0);
    lengthX_ = new LengthSpinBox(ld::core::kCalibrationMinMm, ld::core::kCalibrationMaxMm, 1, 2, 5.0);
    lengthY_ = new LengthSpinBox(ld::core::kCalibrationMinMm, ld::core::kCalibrationMaxMm, 1, 2, 5.0);
    diameter_ = new LengthSpinBox(10.0, ld::core::kCalibrationMaxMm, 1, 2, 5.0);
    lengthX_->setToolTip(tr("Länge der waagerechten Achse der Messgrafik (Vorgabe 100 mm)"));
    lengthY_->setToolTip(tr("Länge der senkrechten Achse der Messgrafik (Vorgabe 100 mm)"));
    diameter_->setToolTip(tr("Durchmesser des Kreises. Sein Mittelpunkt liegt auf der Hälfte beider Achsen."));
    grid->addWidget(lengthX_, 0, 1);
    grid->addWidget(lengthY_, 1, 1);
    grid->addWidget(diameter_, 2, 1);
    graphicLayout->addLayout(grid);
    for (LengthSpinBox* b : {lengthX_, lengthY_, diameter_}) {
        connect(b, &LengthSpinBox::mmChanged, this, &CalibrationDialog::scheduleUpdate);
    }
    graphicGroup->setToolTip(tr("Die Grafik ist ein Winkel mit Teilstrichen alle 10 mm (Zahlen alle 50 mm) und einem Kreis, "
                                "dessen Mittelpunkt auf der Hälfte beider Achsen liegt."));
    auto* insert = new QPushButton(tr("In die Arbeitsfläche einfügen"));
    insert->setToolTip(tr("Ersetzt das Bild auf der Arbeitsfläche durch die Messgrafik. Gebrannt wird danach im "
                          "Hauptfenster mit Start (Cutting, mit Speed und Power aus dem rechten Panel), "
                          "ohne die bereits im Profil hinterlegte Korrektur."));
    insert->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(highlight); color: palette(highlighted-text); font-weight: 600;"
        " border-radius: 3px; padding: 5px 14px; }"));
    connect(insert, &QPushButton::clicked, this, [this] { emit insertRequested(spec()); });
    graphicLayout->addWidget(insert);
    form->addWidget(graphicGroup);

    // ---- Gruppe 2: Messwerte
    auto* measureGroup = new QGroupBox(tr("2  Messwerte"));
    measureGroup->setToolTip(tr("Gemessen werden die tatsächliche Länge der Achse und/oder die Breite bzw. Höhe "
                                "des Kreises. Was nicht gemessen wurde, bleibt leer (–)."));
    auto* measure = new QGridLayout(measureGroup);
    measure->setHorizontalSpacing(8);
    measure->setVerticalSpacing(2);
    measure->setColumnStretch(0, 1);
    measure->addWidget(new QLabel(tr("Soll")), 0, 1, Qt::AlignRight);
    measure->addWidget(new QLabel(tr("Gemessen")), 0, 2);
    measure->addWidget(new QLabel(tr("Abweichung")), 0, 3);
    auto addRow = [&](int row, QLabel*& name, QLabel*& nominal, LengthSpinBox*& box, QLabel*& deviation) {
        name = new QLabel;
        nominal = new QLabel;
        nominal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        box = measuredBox();
        deviation = new QLabel;
        measure->addWidget(name, row, 0);
        measure->addWidget(nominal, row, 1);
        measure->addWidget(box, row, 2);
        measure->addWidget(deviation, row, 3);
        connect(box, &LengthSpinBox::mmChanged, this, &CalibrationDialog::updateResults);
    };
    addRow(1, axisXName_, nominalAxisX_, measuredAxisX_, deviationAxisX_);
    addRow(2, circleXName_, nominalCircleX_, measuredCircleX_, deviationCircleX_);
    addRow(3, axisYName_, nominalAxisY_, measuredAxisY_, deviationAxisY_);
    addRow(4, circleYName_, nominalCircleY_, measuredCircleY_, deviationCircleY_);
    form->addWidget(measureGroup);

    // ---- Gruppe 3: Ergebnis
    auto* resultGroup = new QGroupBox(tr("3  Ergebnis"));
    resultGroup->setToolTip(tr("Waagerecht und senkrecht gehören nach der Aufstellung des Lasers zu je einer Maschinenachse. "
                               "Neuer Faktor = Soll ÷ gemessen (bei Achse und Kreis der Mittelwert)."));
    auto* result = new QGridLayout(resultGroup);
    result->setHorizontalSpacing(10);
    result->setVerticalSpacing(2);
    result->addWidget(new QLabel(tr("Richtung")), 0, 0);
    result->addWidget(new QLabel(tr("Achse")), 0, 1);
    result->addWidget(new QLabel(tr("Im Profil")), 0, 2, Qt::AlignRight);
    result->addWidget(new QLabel(tr("Neu")), 0, 3, Qt::AlignRight);
    auto addResult = [&](int row, const QString& direction, QLabel*& axis, QLabel*& current, QLabel*& fresh) {
        axis = new QLabel;
        current = new QLabel;
        current->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fresh = heading(QString(), resultGroup);
        fresh->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        result->addWidget(new QLabel(direction), row, 0);
        result->addWidget(axis, row, 1);
        result->addWidget(current, row, 2);
        result->addWidget(fresh, row, 3);
    };
    addResult(1, tr("waagerecht"), resultAxisH_, resultCurrentH_, resultNewH_);
    addResult(2, tr("senkrecht"), resultAxisV_, resultCurrentV_, resultNewV_);
    result->setColumnStretch(1, 1);
    saveButton_ = new QPushButton(tr("Im Profil der Maschine hinterlegen"));
    saveButton_->setToolTip(tr("Schreibt die neuen Faktoren ins aktive Maschinenprofil. Danach rechnet die App alle "
                               "Koordinaten damit."));
    connect(saveButton_, &QPushButton::clicked, this, [this] {
        // waagerecht/senkrecht aus Sicht des Bedieners auf die Maschinenachsen abbilden
        const bool horizontalIsX = ld::core::operatorHorizontalIsMachineX(machine_);
        emit saveFactorsRequested(horizontalIsX ? factorHorizontal_ : factorVertical_,
                                  horizontalIsX ? factorVertical_ : factorHorizontal_);
    });
    result->addWidget(saveButton_, 3, 0, 1, 4);
    form->addWidget(resultGroup);
    form->addStretch(1);
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
        positionLabel_->setText(inside ? tr("X %1   Y %2").arg(formatLength(mm.x(), 1), formatLength(mm.y(), 1)) : QString());
    });
    connect(preview_, &TextPreview::zoomChanged, this, [this](double ppm) {
        zoomLabel_->setText(tr("Zoom %1 %").arg(static_cast<int>(std::round(ppm / 96.0 * 25.4 * 100.0))));
    });
    new QShortcut(QKeySequence::ZoomIn, this, [this] { preview_->zoomIn(); });
    new QShortcut(QKeySequence::ZoomOut, this, [this] { preview_->zoomOut(); });

    auto* buttons = new QDialogButtonBox;
    auto* close = buttons->addButton(tr("Schließen"), QDialogButtonBox::RejectRole);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    main->addWidget(buttons);
}

CalibrationSpec CalibrationDialog::spec() const
{
    CalibrationSpec s;
    s.lengthXMm = lengthX_->mm();
    s.lengthYMm = lengthY_->mm();
    s.circleDiameterMm = diameter_->mm();
    return ld::core::sanitized(s);
}

void CalibrationDialog::setMachine(const ld::core::MachineSetup& machine)
{
    machine_ = machine;
    updateLabels();
}

// Beschriftungen: waagerecht/senkrecht mit der zugehörigen Maschinenachse (hängt von der Aufstellung ab).
void CalibrationDialog::updateLabels()
{
    const bool hx = ld::core::operatorHorizontalIsMachineX(machine_);
    const QString h = hx ? tr("Maschinen-X") : tr("Maschinen-Y");
    const QString v = hx ? tr("Maschinen-Y") : tr("Maschinen-X");
    axisXName_->setText(tr("Achse waagerecht"));
    circleXName_->setText(tr("Kreis, Breite"));
    axisYName_->setText(tr("Achse senkrecht"));
    circleYName_->setText(tr("Kreis, Höhe"));
    resultAxisH_->setText(h);
    resultAxisV_->setText(v);
    updatePreview();
    updateResults();
}

void CalibrationDialog::scheduleUpdate()
{
    updateTimer_.start();
}

void CalibrationDialog::updatePreview()
{
    shape_ = ld::core::layoutCalibration(spec());
    preview_->setStrokeShape(shape_.path, shape_.sizeMm, QString());
    const CalibrationSpec s = spec();
    nominalAxisX_->setText(formatLength(s.lengthXMm, 2));
    nominalCircleX_->setText(formatLength(s.circleDiameterMm, 2));
    nominalAxisY_->setText(formatLength(s.lengthYMm, 2));
    nominalCircleY_->setText(formatLength(s.circleDiameterMm, 2));
    updateResults();
}

void CalibrationDialog::updateResults()
{
    const CalibrationSpec s = spec();
    deviationAxisX_->setText(deviationText(s.lengthXMm, measuredAxisX_->mm()));
    deviationCircleX_->setText(deviationText(s.circleDiameterMm, measuredCircleX_->mm()));
    deviationAxisY_->setText(deviationText(s.lengthYMm, measuredAxisY_->mm()));
    deviationCircleY_->setText(deviationText(s.circleDiameterMm, measuredCircleY_->mm()));

    factorHorizontal_ = ld::core::calibrationFactor(s.lengthXMm, measuredAxisX_->mm(), s.circleDiameterMm,
                                                    measuredCircleX_->mm());
    factorVertical_ = ld::core::calibrationFactor(s.lengthYMm, measuredAxisY_->mm(), s.circleDiameterMm,
                                                  measuredCircleY_->mm());
    const bool hx = ld::core::operatorHorizontalIsMachineX(machine_);
    const double currentH = hx ? machine_.correctionX : machine_.correctionY;
    const double currentV = hx ? machine_.correctionY : machine_.correctionX;
    resultCurrentH_->setText(factorText(currentH));
    resultCurrentV_->setText(factorText(currentV));
    resultNewH_->setText(factorHorizontal_ ? factorText(*factorHorizontal_) : QStringLiteral("–"));
    resultNewV_->setText(factorVertical_ ? factorText(*factorVertical_) : QStringLiteral("–"));
    saveButton_->setEnabled(factorHorizontal_ || factorVertical_);
}

void CalibrationDialog::done(int result)
{
    QSettings s;
    s.setValue(QStringLiteral("calibrationDialog/geometry"), saveGeometry());
    const CalibrationSpec c = spec();
    s.setValue(QStringLiteral("calibrationDialog/lengthX"), c.lengthXMm);
    s.setValue(QStringLiteral("calibrationDialog/lengthY"), c.lengthYMm);
    s.setValue(QStringLiteral("calibrationDialog/diameter"), c.circleDiameterMm);
    QDialog::done(result);
}

}  // namespace ld::app
