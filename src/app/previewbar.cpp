#include "previewbar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <algorithm>
#include <QVBoxLayout>

namespace ld::app {

namespace {

// Zeichnet ein Symbol für die Zoom-Knöpfe: Lupe mit Plus oder Minus bzw. vier Ecken (Einpassen).
// Wie beim Hell/Dunkel-Symbol wird in doppelter Auflösung gezeichnet, damit es scharf bleibt.
enum class ZoomSymbol { In, Out, Fit };

QIcon zoomIcon(ZoomSymbol symbol, const QColor& color)
{
    const int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, size * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (symbol == ZoomSymbol::Fit) {
        // Vier Eckwinkel, die einen Rahmen andeuten.
        const double a = size * 0.16;   // Abstand zum Rand
        const double b = size * 0.38;   // Länge der Schenkel bis zur Mitte hin
        const double e = size - a;
        for (const QPointF& corner : {QPointF(a, a), QPointF(e, a), QPointF(a, e), QPointF(e, e)}) {
            const double dx = corner.x() < size / 2.0 ? 1.0 : -1.0;
            const double dy = corner.y() < size / 2.0 ? 1.0 : -1.0;
            painter.drawLine(corner, corner + QPointF(dx * (b - a), 0));
            painter.drawLine(corner, corner + QPointF(0, dy * (b - a)));
        }
    } else {
        // Lupe: Kreis oben links, Griff nach unten rechts, Plus oder Minus im Kreis.
        const QPointF center(size * 0.42, size * 0.42);
        const double radius = size * 0.28;
        painter.drawEllipse(center, radius, radius);
        painter.drawLine(center + QPointF(radius * 0.72, radius * 0.72), QPointF(size * 0.88, size * 0.88));
        const double arm = radius * 0.5;
        painter.drawLine(center - QPointF(arm, 0), center + QPointF(arm, 0));
        if (symbol == ZoomSymbol::In) painter.drawLine(center - QPointF(0, arm), center + QPointF(0, arm));
    }
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

// Knopf, der sich wie eine Fortschrittsanzeige füllt (für die Simulation): Über dem normalen
// Knopf liegt von links her ein Streifen in der Hervorhebungsfarbe des Designs.
class PreviewBar::ProgressButton : public QPushButton {
public:
    using QPushButton::QPushButton;
    void setFraction(double fraction)
    {
        fraction_ = std::clamp(fraction, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPushButton::paintEvent(event);
        if (fraction_ <= 0.0) return;
        QPainter painter(this);
        QColor color = palette().color(QPalette::Highlight);
        color.setAlpha(110);
        const QRect inner = rect().adjusted(1, 1, -1, -1);
        painter.fillRect(QRect(inner.left(), inner.top(), static_cast<int>(inner.width() * fraction_), inner.height()), color);
    }

private:
    double fraction_ = 0.0;
};

PreviewBar::PreviewBar(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 4, 8, 4);
    outer->setSpacing(2);

    // Erste Zeile: Ebenen, Tempo, Simulation.
    auto* row = new QHBoxLayout;
    row->setSpacing(10);

    burnCheck_ = new QCheckBox(tr("Gravur"));
    travelCheck_ = new QCheckBox(tr("Leerfahrt"));
    rapidCheck_ = new QCheckBox(tr("Eilgang"));
    burnCheck_->setChecked(true);
    travelCheck_->setChecked(true);
    rapidCheck_->setChecked(false);
    burnCheck_->setToolTip(tr("Fahrten mit Leistung"));
    travelCheck_->setToolTip(tr("Fahrten ohne Leistung mit Vorschub (Überscan, Lücken)"));
    rapidCheck_->setToolTip(tr("Eilgang (G0), zum Beispiel der Sprung zur nächsten Zeile"));
    row->addWidget(burnCheck_);
    row->addWidget(travelCheck_);
    row->addWidget(rapidCheck_);

    row->addWidget(new QLabel(tr("Tempo")));
    speedCombo_ = new QComboBox;
    speedCombo_->addItem(QStringLiteral("1×"), 1.0);
    speedCombo_->addItem(QStringLiteral("10×"), 10.0);
    speedCombo_->addItem(QStringLiteral("50×"), 50.0);
    speedCombo_->addItem(QStringLiteral("200×"), 200.0);
    speedCombo_->addItem(tr("Maximal"), 2000.0);
    speedCombo_->setCurrentIndex(2);   // Vorgabe 50×
    speedCombo_->setToolTip(tr("Tempo der Simulation (1× = Echtzeit)"));
    row->addWidget(speedCombo_);

    // Simulation auf dem Bildschirm (ohne Laser). Der Knopf wird beim Lauf zu "Stopp".
    simButton_ = new ProgressButton(tr("Simulation"));
    simButton_->setToolTip(tr("Spielt den Auftrag auf dem Bildschirm ab (der Laser wird nicht benutzt)"));
    // Pause und Info sind reine Symbol-Knöpfe (Hinweis steht im Tooltip).
    pauseButton_ = new QPushButton;
    pauseButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    pauseButton_->setToolTip(tr("Pause"));
    pauseButton_->setEnabled(false);
    row->addWidget(simButton_);
    row->addWidget(pauseButton_);
    row->addStretch(1);
    // Zoom (wie im Menü Ansicht): Symbol-Knöpfe, Hinweis im Tooltip.
    zoomInButton_ = new QPushButton;
    zoomInButton_->setToolTip(tr("Vergrößern (Strg++)"));
    zoomOutButton_ = new QPushButton;
    zoomOutButton_->setToolTip(tr("Verkleinern (Strg+-)"));
    fitButton_ = new QPushButton;
    fitButton_->setToolTip(tr("Bild einpassen (Strg+0)"));
    row->addWidget(zoomInButton_);
    row->addWidget(zoomOutButton_);
    row->addWidget(fitButton_);
    updateZoomIcons();
    connect(zoomInButton_, &QPushButton::clicked, this, &PreviewBar::zoomInRequested);
    connect(zoomOutButton_, &QPushButton::clicked, this, &PreviewBar::zoomOutRequested);
    connect(fitButton_, &QPushButton::clicked, this, &PreviewBar::fitRequested);
    // Info: Zeilen, Längen und Größe der Wege stehen in einem kleinen Dialog.
    infoButton_ = new QPushButton;
    infoButton_->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    infoButton_->setToolTip(tr("Info: Zeilen, Längen, Größe und Dauer"));
    row->addWidget(infoButton_);
    outer->addLayout(row);
    connect(infoButton_, &QPushButton::clicked, this, &PreviewBar::infoRequested);

    // Trennlinie zur Arbeitsfläche
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    outer->addWidget(line);

    for (QCheckBox* check : {burnCheck_, travelCheck_, rapidCheck_}) {
        connect(check, &QCheckBox::toggled, this, &PreviewBar::pathLayersChanged);
    }
    connect(speedCombo_, &QComboBox::currentIndexChanged, this, [this] { emit simulationSpeedChanged(simulationSpeed()); });
    connect(simButton_, &QPushButton::clicked, this, &PreviewBar::simulationClicked);
    connect(pauseButton_, &QPushButton::clicked, this, &PreviewBar::simulationPauseClicked);
}

void PreviewBar::updateZoomIcons()
{
    const QColor color = palette().color(QPalette::ButtonText);
    zoomInButton_->setIcon(zoomIcon(ZoomSymbol::In, color));
    zoomOutButton_->setIcon(zoomIcon(ZoomSymbol::Out, color));
    fitButton_->setIcon(zoomIcon(ZoomSymbol::Fit, color));
}

void PreviewBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && zoomInButton_) updateZoomIcons();
}

bool PreviewBar::showBurn() const { return burnCheck_->isChecked(); }
bool PreviewBar::showTravel() const { return travelCheck_->isChecked(); }
bool PreviewBar::showRapid() const { return rapidCheck_->isChecked(); }
double PreviewBar::simulationSpeed() const { return speedCombo_->currentData().toDouble(); }
void PreviewBar::setSimulationProgress(double fraction)
{
    simButton_->setFraction(fraction);
    if (simRunning_) {
        simButton_->setText(tr("Stopp %1 %").arg(static_cast<int>(fraction * 100.0)));
    }
}

void PreviewBar::setSimulationState(bool enabled, bool running, bool paused)
{
    simButton_->setEnabled(enabled);
    if (running != simRunning_) {
        simRunning_ = running;
        simButton_->setFraction(0.0);
        simButton_->setText(running ? tr("Stopp %1 %").arg(0) : tr("Simulation"));
        simButton_->setToolTip(running ? tr("Simulation stoppen")
                                       : tr("Spielt den Auftrag auf dem Bildschirm ab (der Laser wird nicht benutzt)"));
    }
    pauseButton_->setEnabled(running);
    pauseButton_->setIcon(style()->standardIcon(paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    pauseButton_->setToolTip(paused ? tr("Weiter") : tr("Pause"));
}

}  // namespace ld::app
