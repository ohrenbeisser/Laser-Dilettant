// Arbeitsfläche (Meilenstein M1): zeigt das geladene Bild in Millimetern.
//
// Koordinaten in der Szene sind Millimeter, der Ursprung der Szene ist die MITTE des Bildes
// (so bleibt das Bild beim Drehen und Skalieren an seinem Platz). Die Lineale zählen dagegen
// ab der oberen linken Ecke des umschließenden Rechtecks, wie in planung.md 3.8 festgelegt.
//
// Bedienung: Mausrad zoomt (auf die Mausposition), Verschieben mit gedrückter linker oder
// mittlerer Maustaste, Dateien per Drag & Drop ablegen.
#pragma once

#include <QGraphicsView>
#include <QList>
#include <memory>
#include <optional>

#include "core/machinesetup.h"
#include "core/objecttransform.h"
#include "core/pathdata.h"
#include "core/pathrender.h"
#include "progressoverlay.h"
#include "rulerwidget.h"

class QGraphicsPixmapItem;
class QGraphicsRectItem;

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class CanvasView : public QGraphicsView, public RulerSource {
    Q_OBJECT

public:
    explicit CanvasView(ld::core::ImageDocument* document, QWidget* parent = nullptr);

    // Zoom so, dass das ganze Bild sichtbar ist.
    void fitToImage();
    void zoomIn();    // Strg+ und Menü Ansicht
    void zoomOut();   // Strg- und Menü Ansicht

    // Aktueller Zoom: Bildschirmpixel pro Millimeter.
    double pixelsPerMm() const override;

    // Verschiebung zwischen Szene (Bildmitte = 0) und Lineal (Ursprung = 0), in mm.
    // Lineal und Bemaßung beginnen am Job-Ursprung und zählen von dort ins Bild (planung.md 3.23).
    QPointF rulerOffsetMm() const override;
    QPointF rulerSign() const override;

    // ---- Vorschau (M5, siehe planung.md 3.11) ----
    bool hasPaths() const { return paths_ != nullptr; }
    // Laserwege, aus dem G-Code gelesen (nullptr = keine). Werden im Hintergrund gezeichnet.
    void setPaths(std::shared_ptr<const ld::core::PathData> paths);
    // Hinweis oben auf der Arbeitsfläche ("wird erzeugt …", Fehler); leer = keiner.
    void setPathMessage(const QString& message);
    void setPathLayers(bool burn, bool travel, bool rapid);

    // Grüner Fortschritt und Fadenkreuz der Simulation.
    void beginProgress();
    void advanceProgress(const ld::core::PathData& data, size_t first, size_t last, const QPointF& head);
    void hideHead();
    // Fadenkreuz an der gemessenen Laserposition (Szene, mm).
    void showHead(const QPointF& scenePoint);
    void clearProgress();
    // Fernansicht (planung.md 3.43): Das Bild mit dem schon gebrannten Teil als eigenes Bild (längste Seite höchstens maxSidePx),
    // ohne Raster, Lineale und Fadenkreuz. Null, solange kein Fortschritt gezeichnet wird. revision zählt jede Änderung des Fortschritts.
    bool hasProgress() const { return overlay_.isActive(); }
    quint64 progressRevision() const { return overlayRevision_; }
    QImage workSnapshot(int maxSidePx) const;

    // Anfasser am blauen Rahmen: Größe mit der Maus ändern. keepAspect: Seitenverhältnis bleibt erhalten
    // (kommt vom Haken "Seitenverhältnis sperren"). Beim Lauf sind die Anfasser aus.
    void setKeepAspect(bool keep) { keepAspect_ = keep; }
    void setResizeEnabled(bool enabled);

    // NC-Auftrag (planung.md 3.21): Die Arbeitsfläche zeigt die Wege einer Datei statt des Bildes. Der Nullpunkt der Datei
    // ist der Ursprung der Anzeige. bounds = Wege (Szene, mm), danach wird eingepasst. Aus: zurück zum Bild.
    void setNcMode(bool on, const QRectF& bounds = QRectF());
    bool ncMode() const { return ncMode_; }
    // Passt die Ansicht auf einen Bereich (Szene, mm) ein.
    void fitToRect(const QRectF& rect);

    // Zahlenwert der Lineale an einem Punkt der Szene (mm), also die Bemaßung ab dem Job-Ursprung.
    QPointF rulerValueAt(const QPointF& scenePoint) const;

    // Mausposition im Anzeigebereich (Bildschirmpixel); die Lineale zeichnen daraus ihren Strich.
    bool mouseInside() const override { return mouseInside_; }
    QPoint mousePos() const override { return mousePos_; }

signals:
    // Eine Datei wurde auf die Arbeitsfläche gezogen.
    void imageFileDropped(const QString& path);
    // Mausposition in mm ab dem Job-Ursprung (Sicht des Bedieners, Y nach oben); inside = false: Maus draußen.
    void mouseMoved(bool inside, const QPointF& mmFromOrigin);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void onImageReplaced();
    void onTransformChanged();
    void onRasterChanged();
    void updatePixmap();
    void updateSampling();
    void updateModeItems();   // Sichtbarkeit von Bild und weißem Grund
    void schedulePathRender();
    QPointF jobOriginScenePoint() const;
    bool hasContent() const;   // es gibt etwas zu zeigen (Bild oder NC-Auftrag)
    double trackWidthMm() const;
    // Anfasser (Größe ändern per Maus)
    struct Handle { int id; QPoint pos; };
    QList<Handle> handles() const;
    int handleAt(const QPoint& pos) const;
    bool axisAligned() const;   // Drehung ein Vielfaches von 90°: Anfasser an den Kanten sind möglich
    // Grafik zum Nullpunkt verschieben (Ziehen innerhalb des blauen Rahmens, planung.md 3.24)
    bool insideGraphic(const QPoint& pos) const;
    void startMove(const QPoint& mouse);
    void keepNullpointFixed(const QPointF& nullScene);
    void doMove(const QPoint& mouse);
    void endMove();
    void startResize(int handle, const QPoint& mouse);
    void doResize(const QPoint& mouse);
    void endResize();
    void updateHoverCursor(const QPoint& mouse);
    void zoomByFactor(double factor);
    void layoutRulers();
    void updateRulers();

    ld::core::ImageDocument* document_;
    QGraphicsPixmapItem* imageItem_ = nullptr;
    QGraphicsRectItem* boundsItem_ = nullptr;  // umschließendes Rechteck (unsichtbar; Grundlage für Einpassen und Maße)
    QGraphicsRectItem* frameItem_ = nullptr;   // gestrichelter blauer Rahmen um die Grafik, dreht mit
    RulerWidget* horizontalRuler_ = nullptr;
    RulerWidget* verticalRuler_ = nullptr;
    bool keepAspect_ = true;
    bool resizeEnabled_ = true;
    bool resizing_ = false;
    bool moving_ = false;                // Grafik wird verschoben
    QPoint moveStartMouse_;
    QPointF moveStartOffset_;
    QPointF lastNullScene_;              // Lage des Nullpunkts in der Szene beim letzten Mal
    ld::core::ObjectTransform lastTransform_;
    bool hasLastTransform_ = false;
    bool resizeMacroOpen_ = false;
    int resizeHandle_ = -1;
    ld::core::ObjectTransform resizeStart_;
    QSizeF resizeStartBounds_;
    QPoint resizeAnchorView_;       // fester Punkt (Gegenseite) in Bildschirmkoordinaten
    QPoint resizeCornerView_;       // gezogene Ecke beim Start (nur bei schräg gedrehter Grafik)
    QPointF rotatedCorner(int dx, int dy) const;   // Ecke des gedrehten Rahmens (Szene, mm); dx/dy = -1 oder +1
    bool ncMode_ = false;       // NC-Auftrag statt Bild
    QRectF ncBounds_;
    bool panning_ = false;          // Maustaste gedrückt: die Ansicht wird verschoben
    QPoint lastPanPos_;
    QPoint mousePos_;
    bool mouseInside_ = false;

    // Vorschau
    QGraphicsRectItem* paperItem_ = nullptr;   // weißer Grund hinter den Wegen
    std::shared_ptr<const ld::core::PathData> paths_;
    QString pathMessage_;
    ld::core::PathRenderRunner* pathRunner_ = nullptr;
    ld::core::PathRenderOptions pathOptions_;
    QImage pathImage_;        // fertig gezeichnete Wege (Bildschirmgröße zum Zeitpunkt der Rechnung)
    QRectF pathImageRect_;    // Ausschnitt der Szene, den pathImage_ zeigt
    ProgressOverlay overlay_;
    quint64 overlayRevision_ = 0;   // zählt Änderungen des Fortschritts (für die Fernansicht)
    std::optional<QPointF> head_;
};

}  // namespace ld::app
