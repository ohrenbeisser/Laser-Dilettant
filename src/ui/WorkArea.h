#pragma once

#include "core/DeviceProfile.h"

#include <QGraphicsView>
#include <QWidget>

class QGraphicsEllipseItem;
class QGraphicsItemGroup;
class QGraphicsRectItem;
class QHBoxLayout;
class QLabel;

// The graphics view in millimetres. Scene coordinates have Y pointing down;
// machine coordinates are converted according to the profile's origin corner.
class WorkAreaView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit WorkAreaView(QWidget* parent = nullptr);

    void setProfile(const DeviceProfile& profile);
    void setLaserPosition(double x, double y, bool visible);
    void fitAll();

    QPointF machineToScene(double x, double y) const;
    QPointF sceneToMachine(const QPointF& p) const;

signals:
    void mouseMachinePosition(double x, double y, bool inside);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void rebuildScene();
    double gridStepFor(double pixelsPerMm) const;

    DeviceProfile m_profile;
    QGraphicsRectItem* m_areaItem = nullptr;
    QGraphicsItemGroup* m_crosshair = nullptr;
    bool m_panning = false;
    bool m_spaceHeld = false;
    bool m_fitPending = true;
    QPoint m_lastPan;
};

// Work area with a small tool row (fit, mouse position).
class WorkArea : public QWidget
{
    Q_OBJECT
public:
    explicit WorkArea(QWidget* parent = nullptr);

    WorkAreaView* view() const { return m_view; }
    void setProfile(const DeviceProfile& profile) { m_view->setProfile(profile); }
    void setLaserPosition(double x, double y, bool visible) { m_view->setLaserPosition(x, y, visible); }
    void fitAll() { m_view->fitAll(); }
    // Adds a widget at the right end of the tool row.
    void addToolWidget(QWidget* w);

private:
    WorkAreaView* m_view;
    QHBoxLayout* m_toolRow;
    QLabel* m_mouse;
};
