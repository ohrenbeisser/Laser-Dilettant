#include "ui/WorkArea.h"

#include "core/Units.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMargin = 25.0; // mm around the work area
}

WorkAreaView::WorkAreaView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::NoDrag);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMinimumSize(300, 250);
}

QPointF WorkAreaView::machineToScene(double x, double y) const
{
    const double W = m_profile.workAreaX, H = m_profile.workAreaY;
    switch (m_profile.origin) {
    case Origin::BottomLeft: return { x, H - y };
    case Origin::TopLeft: return { x, y };
    case Origin::TopRight: return { W - x, y };
    case Origin::BottomRight: return { W - x, H - y };
    }
    return { x, H - y };
}

QPointF WorkAreaView::sceneToMachine(const QPointF& p) const
{
    // The mapping is its own inverse.
    return machineToScene(p.x(), p.y());
}

void WorkAreaView::setProfile(const DeviceProfile& profile)
{
    const bool sizeChanged = profile.workAreaX != m_profile.workAreaX || profile.workAreaY != m_profile.workAreaY
        || profile.origin != m_profile.origin;
    m_profile = profile;
    rebuildScene();
    if (sizeChanged)
        fitAll();
}

void WorkAreaView::rebuildScene()
{
    QGraphicsScene* s = scene();
    s->clear();
    m_crosshair = nullptr;
    const QRectF area(0, 0, m_profile.workAreaX, m_profile.workAreaY);
    s->setSceneRect(area.adjusted(-kMargin, -kMargin, kMargin, kMargin));

    QPen border(palette().color(QPalette::Text));
    border.setCosmetic(true);
    border.setWidthF(1.5);
    m_areaItem = s->addRect(area, border, Qt::NoBrush);
    m_areaItem->setZValue(1);

    // Laser position: cross-hair with circle, drawn with cosmetic pens.
    QPen red(QColor(0xff, 0x17, 0x44));
    red.setCosmetic(true);
    red.setWidthF(1.5);
    auto* group = new QGraphicsItemGroup;
    auto* h = new QGraphicsLineItem(-6, 0, 6, 0);
    auto* v = new QGraphicsLineItem(0, -6, 0, 6);
    auto* c = new QGraphicsEllipseItem(-2.5, -2.5, 5, 5);
    c->setPen(red);
    h->setPen(red);
    v->setPen(red);
    group->addToGroup(h);
    group->addToGroup(v);
    group->addToGroup(c);
    group->setZValue(100);
    group->setVisible(false);
    s->addItem(group);
    m_crosshair = group;
}

void WorkAreaView::setLaserPosition(double x, double y, bool visible)
{
    if (!m_crosshair)
        return;
    m_crosshair->setPos(machineToScene(x, y));
    m_crosshair->setVisible(visible);
}

void WorkAreaView::fitAll()
{
    if (!scene())
        return;
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    // Before the first show the viewport has no real size yet.
    m_fitPending = !isVisible();
}

double WorkAreaView::gridStepFor(double pixelsPerMm) const
{
    // Fine grid every 10 mm, but avoid lines closer than ~6 px.
    const double steps[] = { 10, 50, 100, 500 };
    for (double s : steps)
        if (s * pixelsPerMm >= 6)
            return s;
    return 1000;
}

void WorkAreaView::drawBackground(QPainter* p, const QRectF& rect)
{
    const QPalette pal = palette();
    p->fillRect(rect, pal.color(QPalette::Window).darker(108));
    const QRectF area(0, 0, m_profile.workAreaX, m_profile.workAreaY);
    p->fillRect(area, pal.color(QPalette::Base));

    const double ppm = transform().m11();
    const double fine = gridStepFor(ppm);
    const double coarse = fine < 50 ? 50 : fine * 5;
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    QPen finePen(dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 30));
    finePen.setCosmetic(true);
    QPen coarsePen(dark ? QColor(255, 255, 255, 70) : QColor(0, 0, 0, 75));
    coarsePen.setCosmetic(true);

    // Lines are placed in machine coordinates so that the grid starts at the
    // origin corner, whatever corner that is.
    auto drawLines = [&](double step, const QPen& pen) {
        p->setPen(pen);
        for (double m = 0; m <= m_profile.workAreaX + 1e-6; m += step) {
            const QPointF a = machineToScene(m, 0);
            p->drawLine(QPointF(a.x(), 0), QPointF(a.x(), m_profile.workAreaY));
        }
        for (double m = 0; m <= m_profile.workAreaY + 1e-6; m += step) {
            const QPointF a = machineToScene(0, m);
            p->drawLine(QPointF(0, a.y()), QPointF(m_profile.workAreaX, a.y()));
        }
    };
    if (fine < coarse)
        drawLines(fine, finePen);
    drawLines(coarse, coarsePen);
}

void WorkAreaView::drawForeground(QPainter* p, const QRectF&)
{
    // Axis labels and origin marker in screen pixels.
    const double ppm = transform().m11();
    const double fine = gridStepFor(ppm);
    double labelStep = fine < 50 ? 50 : fine * 5;
    if (labelStep * ppm < 40)
        labelStep *= 2;
    p->save();
    p->resetTransform();
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p->setFont(f);
    p->setPen(palette().color(QPalette::Text));
    const QFontMetrics fm(f);
    const double edgeY = m_profile.origin == Origin::TopLeft || m_profile.origin == Origin::TopRight ? 0 : m_profile.workAreaY;
    const double edgeX = m_profile.origin == Origin::TopRight || m_profile.origin == Origin::BottomRight ? m_profile.workAreaX : 0;
    const bool labelsBelow = edgeY > 0;
    const bool labelsLeft = edgeX == 0;
    for (double m = 0; m <= m_profile.workAreaX + 1e-6; m += labelStep) {
        const QPointF s = machineToScene(m, 0);
        const QPoint v = mapFromScene(QPointF(s.x(), edgeY));
        const QString t = QString::number(m);
        const int w = fm.horizontalAdvance(t);
        p->drawText(QPoint(v.x() - w / 2, labelsBelow ? v.y() + fm.ascent() + 3 : v.y() - 4), t);
    }
    for (double m = labelStep; m <= m_profile.workAreaY + 1e-6; m += labelStep) {
        const QPointF s = machineToScene(0, m);
        const QPoint v = mapFromScene(QPointF(edgeX, s.y()));
        const QString t = QString::number(m);
        const int w = fm.horizontalAdvance(t);
        p->drawText(QPoint(labelsLeft ? v.x() - w - 4 : v.x() + 4, v.y() + fm.ascent() / 2), t);
    }

    // Origin: two short arrows for +X and +Y.
    const QPoint o = mapFromScene(machineToScene(0, 0));
    const QPoint ax = mapFromScene(machineToScene(std::min(20.0, m_profile.workAreaX / 4), 0));
    const QPoint ay = mapFromScene(machineToScene(0, std::min(20.0, m_profile.workAreaY / 4)));
    QPen axisPen(QColor(0xe5, 0x39, 0x35), 2);
    p->setPen(axisPen);
    p->drawLine(o, ax);
    p->setPen(QPen(QColor(0x43, 0xa0, 0x47), 2));
    p->drawLine(o, ay);
    p->setPen(palette().color(QPalette::Text));
    p->drawText(ax + QPoint(3, 4), QStringLiteral("X"));
    p->drawText(ay + QPoint(-4, -3), QStringLiteral("Y"));
    p->setBrush(palette().color(QPalette::Text));
    p->drawEllipse(o, 3, 3);
    p->restore();
}

void WorkAreaView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    const double factor = std::pow(1.2, steps);
    const double current = transform().m11();
    const double target = std::clamp(current * factor, 0.2, 400.0);
    scale(target / current, target / current);
    event->accept();
}

void WorkAreaView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_panning = true;
        m_lastPan = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void WorkAreaView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF m = sceneToMachine(mapToScene(event->position().toPoint()));
    const bool inside = m.x() >= 0 && m.y() >= 0 && m.x() <= m_profile.workAreaX && m.y() <= m_profile.workAreaY;
    emit mouseMachinePosition(m.x(), m.y(), inside);
    if (m_panning) {
        const QPoint now = event->position().toPoint();
        const QPoint d = now - m_lastPan;
        m_lastPan = now;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void WorkAreaView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void WorkAreaView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void WorkAreaView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        if (!m_panning)
            setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void WorkAreaView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitPending)
        fitAll();
}

void WorkAreaView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    if (m_fitPending)
        QTimer::singleShot(0, this, &WorkAreaView::fitAll);
}

void WorkAreaView::leaveEvent(QEvent* event)
{
    emit mouseMachinePosition(0, 0, false);
    QGraphicsView::leaveEvent(event);
}

WorkArea::WorkArea(QWidget* parent)
    : QWidget(parent)
    , m_view(new WorkAreaView(this))
    , m_mouse(new QLabel(this))
{
    auto* fit = new QToolButton(this);
    fit->setText(tr("Show all"));
    fit->setToolTip(tr("Show the whole work area (Ctrl+0)"));
    fit->setAutoRaise(true);
    connect(fit, &QToolButton::clicked, m_view, &WorkAreaView::fitAll);

    auto* hint = new QLabel(tr("Zoom: mouse wheel · Pan: middle mouse button or Space + drag"), this);
    hint->setEnabled(false);

    auto* top = new QHBoxLayout;
    m_toolRow = top;
    top->setContentsMargins(4, 2, 4, 0);
    top->addWidget(fit);
    top->addSpacing(8);
    top->addWidget(hint);
    top->addStretch(1);
    top->addWidget(m_mouse);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addLayout(top);
    l->addWidget(m_view, 1);

    connect(m_view, &WorkAreaView::mouseMachinePosition, this, [this](double x, double y, bool inside) {
        m_mouse->setText(inside ? QStringLiteral("X %1  Y %2 mm").arg(Units::display(x, 1), Units::display(y, 1)) : QString());
    });
}

void WorkArea::addToolWidget(QWidget* w)
{
    w->setParent(this);
    m_toolRow->addWidget(w);
}
