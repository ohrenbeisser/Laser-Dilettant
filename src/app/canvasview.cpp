#include "canvasview.h"

#include <cmath>

#include <QDragEnterEvent>
#include <QPainterPath>
#include <QDropEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>

#include "appsettings.h"
#include "displayutil.h"
#include "core/imagedocument.h"
#include "core/machinesetup.h"
#include "core/objecttransform.h"
#include "core/rulerscale.h"
#include "rulerwidget.h"

namespace ld::app {

namespace {

constexpr double kMinZoom = 0.05;       // Bildschirmpixel pro mm
constexpr double kMaxZoom = 400.0;
constexpr double kZoomStep = 1.2;       // ein Schritt (Mausrad-Raste, Strg+ und Strg-)
constexpr double kSceneHalfSizeMm = 20000.0;  // die Szene ist fest und riesig, damit nichts springt

}  // namespace

// ---------------------------------------------------------------------------------------------
// CanvasView
// ---------------------------------------------------------------------------------------------
CanvasView::CanvasView(ld::core::ImageDocument* document, QWidget* parent)
    : QGraphicsView(parent), document_(document)
{
    auto* scene = new QGraphicsScene(this);
    scene->setSceneRect(-kSceneHalfSizeMm, -kSceneHalfSizeMm, 2 * kSceneHalfSizeMm, 2 * kSceneHalfSizeMm);
    setScene(scene);

    // Weißer Grund hinter dem Bild; sichtbar, wenn die Laserwege statt (oder über) dem Bild gezeigt werden.
    paperItem_ = scene->addRect(QRectF(), Qt::NoPen, Qt::white);
    paperItem_->setZValue(-1.0);
    paperItem_->setOpacity(AppSettings::instance().colors().paperOpacity / 100.0);   // Deckkraft des weißen Grundes (Einstellungen, Vorgabe 50 %)
    paperItem_->setVisible(false);

    imageItem_ = scene->addPixmap(QPixmap());
    imageItem_->setTransformationMode(Qt::SmoothTransformation);

    boundsItem_ = scene->addRect(QRectF());
    // Gestrichelte blaue Linie um die ganze Grafik (in hellem und dunklem Design gut sichtbar).
    QPen boundsPen(QColor(0x2f, 0x80, 0xff));
    boundsPen.setWidthF(1.5);
    boundsPen.setCosmetic(true);  // bleibt beim Zoomen gleich dünn
    boundsPen.setStyle(Qt::DashLine);
    boundsItem_->setPen(Qt::NoPen);   // nur noch Rechengrundlage; sichtbar ist der gedrehte Rahmen
    boundsItem_->setVisible(false);
    frameItem_ = scene->addRect(QRectF());
    frameItem_->setPen(boundsPen);
    frameItem_->setVisible(false);

    setRenderHint(QPainter::SmoothPixmapTransform);  // gilt nur, wenn der Bildknoten "glatt" fordert
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // Verschieben mit gedrückter Maustaste machen wir selbst (siehe mousePressEvent): Der Mauszeiger ist ein
    // Fadenkreuz und wird erst beim Verschieben zur geschlossenen Hand.
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::CrossCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);

    // Zoom auf "20 Pixel pro mm" als Startwert; wird beim ersten Bild angepasst.
    scale(2.0, 2.0);

    setViewportMargins(kRulerThickness, kRulerThickness, 0, 0);
    horizontalRuler_ = new RulerWidget(Qt::Horizontal, this, this);
    verticalRuler_ = new RulerWidget(Qt::Vertical, this, this);
    layoutRulers();

    // Mit der Mitte (0,0) beginnen, damit auch ohne Bild etwas Sinnvolles zu sehen ist.
    centerOn(0, 0);

    connect(document_, &ld::core::ImageDocument::imageReplaced, this, &CanvasView::onImageReplaced);
    connect(document_, &ld::core::ImageDocument::transformChanged, this, &CanvasView::onTransformChanged);
    connect(document_, &ld::core::ImageDocument::rasterChanged, this, &CanvasView::onRasterChanged);
    // Der Job-Ursprung bestimmt die Lage der Markierungen.
    connect(document_, &ld::core::ImageDocument::engravingChanged, this, [this] {
        viewport()->update();
        horizontalRuler_->update();   // der Job-Ursprung bestimmt die Zählrichtung
        verticalRuler_->update();
    });

    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    // Einstellungen: Farben, Raster und Einheiten wirken sofort.
    connect(&AppSettings::instance(), &AppSettings::colorsChanged, this, [this] {
        paperItem_->setBrush(AppSettings::instance().colors().imagePaper);
        paperItem_->setOpacity(AppSettings::instance().colors().paperOpacity / 100.0);
        updatePixmap();
        schedulePathRender();
        viewport()->update();
    });
    connect(&AppSettings::instance(), &AppSettings::unitsChanged, this, [this] {
        updateRulers();
        viewport()->update();
    });
    paperItem_->setBrush(AppSettings::instance().colors().imagePaper);

    pathRunner_ = new ld::core::PathRenderRunner(120, this);
    connect(pathRunner_, &ld::core::PathRenderRunner::finished, this,
            [this](const QImage& image, const QRectF& sceneRect) {
                pathImage_ = image;
                pathImageRect_ = sceneRect;
                viewport()->update();
            });
}

double CanvasView::pixelsPerMm() const
{
    return transform().m11();
}

void CanvasView::onImageReplaced()
{
    updatePixmap();
    onTransformChanged();
    fitToImage();
}

void CanvasView::onRasterChanged()
{
    updatePixmap();
    onTransformChanged();  // der Größenfaktor hängt von den Pixeln des Rasters ab
}

// Zeigt das berechnete Raster (Schwarz/Weiß, Dithering ...). Solange es noch nicht fertig ist,
// steht das Originalbild da.
void CanvasView::updatePixmap()
{
    if (!document_->hasImage()) {
        imageItem_->setPixmap(QPixmap());
        return;
    }
    const DisplayColors& colors = AppSettings::instance().colors();
    // Die Farben sind deckend; nur das Weiß des Bildes ist durchsichtig (der Grund dahinter ist es halb).
    const QImage shown = overlayRaster(document_->raster().isNull() ? document_->image() : document_->raster(), colors.imageBurn);
    imageItem_->setPixmap(QPixmap::fromImage(shown));
    updateSampling();
}

// Ab etwa 1:1 die Pixel scharf zeigen (Dither-Punkte erkennbar), kleiner geglättet.
void CanvasView::updateSampling()
{
    const QSize px = imageItem_->pixmap().size();
    if (px.isEmpty() || !document_->hasImage()) {
        return;
    }
    const double screenPerPixel = pixelsPerMm() * document_->transform().widthMm / px.width();
    imageItem_->setTransformationMode(screenPerPixel >= 1.0 ? Qt::FastTransformation
                                                           : Qt::SmoothTransformation);
}

// Schiebt die Ansicht so, dass der Nullpunkt (neue Lage in der Szene: nullScene) an derselben Stelle des Bildschirms bleibt.
void CanvasView::keepNullpointFixed(const QPointF& nullScene)
{
    const QPoint before = mapFromScene(lastNullScene_);
    const QPoint now = mapFromScene(nullScene);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (now.x() - before.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + (now.y() - before.y()));
}

void CanvasView::onTransformChanged()
{
    if (!document_->hasImage()) {
        imageItem_->setVisible(false);
        paperItem_->setVisible(false);
        boundsItem_->setVisible(false);
        frameItem_->setVisible(false);
        hasLastTransform_ = false;
        updateRulers();
        viewport()->update();
        return;
    }
    const ld::core::ObjectTransform& t = document_->transform();
    const QSize px = imageItem_->pixmap().size();
    if (px.isEmpty()) {
        return;
    }

    // Reihenfolge (von rechts nach links angewendet): Bildmitte in den Ursprung schieben,
    // auf Millimeter skalieren und ggf. spiegeln, dann um die Mitte drehen.
    QTransform matrix;
    matrix.rotate(t.rotationDeg);
    matrix.scale((t.flipHorizontal ? -1.0 : 1.0) * t.widthMm / px.width(),
                 (t.flipVertical ? -1.0 : 1.0) * t.heightMm / px.height());
    matrix.translate(-px.width() / 2.0, -px.height() / 2.0);
    imageItem_->setTransform(matrix);
    imageItem_->setVisible(true);
    paperItem_->setTransform(matrix);
    paperItem_->setRect(0, 0, px.width(), px.height());
    updateModeItems();

    const QSizeF bounds = ld::core::boundingSize(t);
    boundsItem_->setRect(-bounds.width() / 2.0, -bounds.height() / 2.0, bounds.width(), bounds.height());
    boundsItem_->setVisible(true);
    // Der blaue Rahmen gehört zur Grafik: gleiche Drehung und Spiegelung wie das Bild.
    frameItem_->setTransform(matrix);
    frameItem_->setRect(0, 0, px.width(), px.height());
    frameItem_->setVisible(true);

    // Wurde nur die Lage zum Nullpunkt geändert, bleibt der Nullpunkt auf dem Bildschirm stehen: Die Grafik wandert,
    // Lineale und Raster nicht.
    const QPointF nullScene = jobOriginScenePoint();
    if (hasLastTransform_) {
        ld::core::ObjectTransform a = lastTransform_, b = t;
        a.offsetXMm = a.offsetYMm = b.offsetXMm = b.offsetYMm = 0.0;
        if (a == b && (lastTransform_.offsetXMm != t.offsetXMm || lastTransform_.offsetYMm != t.offsetYMm)) {
            keepNullpointFixed(nullScene);
        }
    }
    lastNullScene_ = nullScene;
    lastTransform_ = t;
    hasLastTransform_ = true;
    updateRulers();
    viewport()->update();
}

void CanvasView::setNcMode(bool on, const QRectF& bounds)
{
    ncMode_ = on;
    ncBounds_ = bounds;
    clearProgress();
    if (on) {
        imageItem_->setVisible(false);
        paperItem_->setVisible(false);
        boundsItem_->setVisible(false);
        frameItem_->setVisible(false);
        updateRulers();
        viewport()->update();
        fitToImage();
    } else {
        // zurück zum Bild: Elemente und Lineal neu aufbauen
        updateModeItems();
        onTransformChanged();
        fitToImage();
    }
}

void CanvasView::fitToRect(const QRectF& rect)
{
    // Etwas Rand lassen; ein leeres Rechteck (zum Beispiel eine einzelne Stelle) wird zu einem kleinen Bereich.
    QRectF r = rect;
    if (r.width() < 10.0) r.adjust(-5.0, 0.0, 5.0, 0.0);
    if (r.height() < 10.0) r.adjust(0.0, -5.0, 0.0, 5.0);
    const QRectF padded = r.adjusted(-r.width() * 0.06, -r.height() * 0.06, r.width() * 0.06, r.height() * 0.06);
    fitInView(padded, Qt::KeepAspectRatio);
    const double zoom = pixelsPerMm();
    if (zoom > kMaxZoom) {
        scale(kMaxZoom / zoom, kMaxZoom / zoom);
    }
    updateSampling();
    updateRulers();
}

void CanvasView::fitToImage()
{
    if (ncMode_) {
        fitToRect(ncBounds_.isNull() ? QRectF(-50, -50, 100, 100) : ncBounds_);
        return;
    }
    if (!document_->hasImage()) {
        return;
    }
    // Etwas Rand um das umschließende Rechteck lassen.
    const QRectF rect = boundsItem_->rect();
    const QRectF padded = rect.adjusted(-rect.width() * 0.06, -rect.height() * 0.06,
                                        rect.width() * 0.06, rect.height() * 0.06);
    fitInView(padded, Qt::KeepAspectRatio);
    // Zoom begrenzen (winzige Bilder nicht ins Unendliche aufziehen).
    const double zoom = pixelsPerMm();
    if (zoom > kMaxZoom) {
        scale(kMaxZoom / zoom, kMaxZoom / zoom);
    }
    updateSampling();
    updateRulers();
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        return;
    }
    zoomByFactor(std::pow(kZoomStep, steps));
    event->accept();
}

// Zoomen um einen Faktor, begrenzt auf den erlaubten Bereich. Das Mausrad zoomt zur Mausposition
// (AnchorUnderMouse); Tastatur und Menü zoomen zur Mitte der Ansicht.
void CanvasView::zoomByFactor(double factor)
{
    const double target = pixelsPerMm() * factor;
    if (target < kMinZoom) {
        factor = kMinZoom / pixelsPerMm();
    } else if (target > kMaxZoom) {
        factor = kMaxZoom / pixelsPerMm();
    }
    scale(factor, factor);
    updateSampling();
    updateRulers();
}

void CanvasView::zoomIn()
{
    const auto anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    zoomByFactor(kZoomStep);
    setTransformationAnchor(anchor);
}

void CanvasView::zoomOut()
{
    const auto anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    zoomByFactor(1.0 / kZoomStep);
    setTransformationAnchor(anchor);
}

void CanvasView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    layoutRulers();
}

void CanvasView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    updateRulers();
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    // Linke oder mittlere Maustaste verschiebt die Ansicht; solange sie gedrückt ist, zeigt der Mauszeiger die Hand.
    if (event->button() == Qt::LeftButton) {
        const int handle = handleAt(event->pos());
        if (handle >= 0) {
            startResize(handle, event->pos());
            event->accept();
            return;
        }
    }
    // Ziehen innerhalb der Grafik verschiebt sie zum Nullpunkt; außerhalb (und mit der mittleren Taste) die Ansicht.
    if (event->button() == Qt::LeftButton && insideGraphic(event->pos())) {
        startMove(event->pos());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPanPos_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (resizing_ && event->button() == Qt::LeftButton) {
        endResize();
        updateHoverCursor(event->pos());
        event->accept();
        return;
    }
    if (moving_ && event->button() == Qt::LeftButton) {
        endMove();
        event->accept();
        return;
    }
    if (panning_ && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        panning_ = false;
        viewport()->setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::layoutRulers()
{
    horizontalRuler_->setGeometry(kRulerThickness, 0, viewport()->width(), kRulerThickness);
    verticalRuler_->setGeometry(0, kRulerThickness, kRulerThickness, viewport()->height());
    updateRulers();
}

void CanvasView::updateRulers()
{
    schedulePathRender();   // Zoom oder Verschiebung geändert: Laserwege neu zeichnen
    if (horizontalRuler_) {
        horizontalRuler_->update();
    }
    if (verticalRuler_) {
        verticalRuler_->update();
    }
}

// --- Drag & Drop --------------------------------------------------------------------------------
void CanvasView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void CanvasView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void CanvasView::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            emit imageFileDropped(url.toLocalFile());
            return;  // nur die erste Datei (es gibt höchstens ein Bild)
        }
    }
}

// --- Zeichnen -----------------------------------------------------------------------------------
void CanvasView::drawBackground(QPainter* painter, const QRectF& rect)
{
    // Hintergrund jedes Mal aus der Palette holen, damit Hell/Dunkel-Wechsel sofort wirkt.
    painter->fillRect(rect, palette().color(QPalette::AlternateBase));
    const DisplayColors& colors = AppSettings::instance().colors();
    if (!colors.gridVisible || colors.gridStrength <= 0) {
        return;
    }
    // Das Raster folgt der Skala des Lineals (gewählte Einheit, runde Schritte).
    const double unit = mmPerLengthUnit();
    const double ppm = pixelsPerMm();
    const ld::core::RulerScale scale = ld::core::rulerScaleFor(ppm * unit);
    const double majorStep = scale.majorMm * unit;   // in mm
    const double step = scale.minorMm() * unit * ppm >= 6.0 ? scale.minorMm() * unit : majorStep;

    // Dezent: die feinen Linien kaum sichtbar, die an den beschrifteten Linealstrichen etwas kräftiger.
    const double strength = colors.gridStrength / 100.0;
    QColor line = colors.grid;
    line.setAlpha(static_cast<int>(255 * strength * 0.45));
    QPen minorPen(line);
    minorPen.setCosmetic(true);
    line.setAlpha(static_cast<int>(255 * strength * 0.90));
    QPen majorPen(line);
    majorPen.setCosmetic(true);

    painter->setRenderHint(QPainter::Antialiasing, false);
    // Linien liegen auf Vielfachen des Schritts im Lineal-Koordinatensystem.
    auto drawLines = [&](bool vertical) {
        const double offset = vertical ? rulerOffsetMm().x() : rulerOffsetMm().y();
        const double sign = vertical ? rulerSign().x() : rulerSign().y();
        const double a = sign * (vertical ? rect.left() : rect.top()) + offset;
        const double b = sign * (vertical ? rect.right() : rect.bottom()) + offset;
        const double from = std::min(a, b);
        const double to = std::max(a, b);
        const long first = static_cast<long>(std::floor(from / step));
        const long last = static_cast<long>(std::ceil(to / step));
        for (long i = first; i <= last; ++i) {
            const double mm = i * step;
            const double scenePos = (mm - offset) / sign;
            const double rest = std::fmod(std::abs(mm), majorStep);
            const bool major = rest < step * 0.01 || std::abs(rest - majorStep) < step * 0.01;
            painter->setPen(major ? majorPen : minorPen);
            if (vertical) {
                painter->drawLine(QPointF(scenePos, rect.top()), QPointF(scenePos, rect.bottom()));
            } else {
                painter->drawLine(QPointF(rect.left(), scenePos), QPointF(rect.right(), scenePos));
            }
        }
    };
    drawLines(true);
    drawLines(false);
}

// ---------------------------------------------------------------------------------------------
// Vorschau: Anzeige-Modi, Laserwege, Fortschritt, Markierungen
// ---------------------------------------------------------------------------------------------
// Die Vorschau zeigt immer das Bild (deckende Farben, durchsichtiges Weiß) auf dem halb durchsichtigen weißen Grund,
// die Laserwege darüber.
void CanvasView::updateModeItems()
{
    const bool hasImage = document_->hasImage() && !imageItem_->pixmap().isNull() && !ncMode_;
    paperItem_->setVisible(hasImage);
    imageItem_->setVisible(hasImage);
    imageItem_->setOpacity(1.0);   // die Farben sind deckend (nur das Weiß des Bildes ist durchsichtig)
}

void CanvasView::setPaths(std::shared_ptr<const ld::core::PathData> paths)
{
    paths_ = std::move(paths);
    pathImage_ = QImage();
    schedulePathRender();
    viewport()->update();
}

void CanvasView::setPathMessage(const QString& message)
{
    pathMessage_ = message;
    viewport()->update();
}

void CanvasView::setPathLayers(bool burn, bool travel, bool rapid)
{
    pathOptions_.showBurn = burn;
    pathOptions_.showTravel = travel;
    pathOptions_.showRapid = rapid;
    schedulePathRender();
}

// Die Wege werden für den sichtbaren Ausschnitt in Bildschirmauflösung gezeichnet (im Hintergrund,
// kurz nach der letzten Änderung). Bis dahin bleibt das alte Bild stehen und wird mitverschoben.
void CanvasView::schedulePathRender()
{
    if (!pathRunner_) {
        return;
    }
    if (!paths_) {
        pathRunner_->cancel();
        return;
    }
    ld::core::PathRenderOptions options = pathOptions_;
    options.burnWidthMm = trackWidthMm();
    // Cutting-Linien sind sehr schmal: auf dem Bildschirm mindestens 3 Pixel breit, damit sie deutlich zu sehen sind.
    options.burnMinWidthPx = document_->operation().mode == ld::core::EngraveMode::Cutting ? 3.0 : 1.0;
    const DisplayColors& colors = AppSettings::instance().colors();
    options.burnColor = colors.burn;
    options.travelColor = colors.travel;
    options.rapidColor = colors.rapid;
    pathRunner_->request(paths_, viewport()->size(), viewportTransform(),
                         mapToScene(viewport()->rect()).boundingRect(), options);
}

// Breite der gebrannten Spur in der Vorschau: die Pixelgröße (bei Full ist das auch der Abstand der Füllzeilen).
double CanvasView::trackWidthMm() const
{
    if (ncMode_) return 0.1;   // Breite der Spur bei einer fremden Datei: etwa ein Laserpunkt
    return document_->settings().pixelSizeMm;
}

bool CanvasView::hasContent() const
{
    return ncMode_ || document_->hasImage();
}

QPointF CanvasView::rulerSign() const
{
    if (ncMode_) return QPointF(1.0, -1.0);          // Datei: wie die Maschine, Y nach oben
    if (!document_->hasImage()) return QPointF(1.0, 1.0);
    return ld::core::jobOriginRulerSign(document_->jobOrigin());
}

QPointF CanvasView::rulerOffsetMm() const
{
    // Zahl = Vorzeichen * Szene + Verschiebung; die Null liegt am Job-Ursprung.
    if (ncMode_ || !document_->hasImage()) return QPointF(0.0, 0.0);
    const QPointF sign = rulerSign();
    const QPointF origin = jobOriginScenePoint();
    return QPointF(-sign.x() * origin.x(), -sign.y() * origin.y());
}

QPointF CanvasView::rulerValueAt(const QPointF& scenePoint) const
{
    const QPointF sign = rulerSign();
    const QPointF offset = rulerOffsetMm();
    return QPointF(sign.x() * scenePoint.x() + offset.x(), sign.y() * scenePoint.y() + offset.y());
}

QPointF CanvasView::jobOriginScenePoint() const
{
    if (ncMode_) return QPointF(0.0, 0.0);   // der Nullpunkt der Datei
    const QSizeF bounds = ld::core::boundingSize(document_->transform());
    const QPointF fraction = ld::core::jobOriginFraction(document_->jobOrigin());
    const ld::core::ObjectTransform& t = document_->transform();
    // Der Job-Ursprung der Grafik liegt um den Versatz neben dem Nullpunkt (planung.md 3.24).
    return QPointF(-bounds.width() / 2.0 + fraction.x() * bounds.width() - t.offsetXMm,
                   -bounds.height() / 2.0 + fraction.y() * bounds.height() - t.offsetYMm);
}

void CanvasView::beginProgress()
{
    ++overlayRevision_;
    overlay_.setColor(AppSettings::instance().colors().progress);
    const QSizeF bounds = ld::core::boundingSize(document_->transform());
    overlay_.reset(ncMode_ ? ncBounds_.adjusted(-1.0, -1.0, 1.0, 1.0)
                           : QRectF(-bounds.width() / 2.0, -bounds.height() / 2.0, bounds.width(), bounds.height()),
                   trackWidthMm());
    head_.reset();
    viewport()->update();
}

void CanvasView::advanceProgress(const ld::core::PathData& data, size_t first, size_t last, const QPointF& head)
{
    overlay_.paint(data, first, last);
    ++overlayRevision_;
    head_ = head;
    viewport()->update();
}

void CanvasView::showHead(const QPointF& scenePoint)
{
    head_ = scenePoint;
    viewport()->update();
}

void CanvasView::hideHead()
{
    head_.reset();
    viewport()->update();
}

void CanvasView::clearProgress()
{
    ++overlayRevision_;
    overlay_.clear();
    head_.reset();
    viewport()->update();
}

QImage CanvasView::workSnapshot(int maxSidePx) const
{
    if (!overlay_.isActive() || maxSidePx < 16) return QImage();
    const QRectF rect = overlay_.rect();
    const double longSide = std::max(rect.width(), rect.height());
    if (longSide <= 0.0) return QImage();
    const double scale = std::min(maxSidePx / longSide, 40.0);
    QImage out(std::max(1, static_cast<int>(std::ceil(rect.width() * scale))),
               std::max(1, static_cast<int>(std::ceil(rect.height() * scale))), QImage::Format_RGB32);
    const DisplayColors& colors = AppSettings::instance().colors();
    out.fill(colors.imagePaper);
    QTransform sceneToImage;
    sceneToImage.scale(scale, scale);
    sceneToImage.translate(-rect.left(), -rect.top());

    if (ncMode_) {
        // NC-Auftrag: die Laserwege statt eines Bildes
        if (paths_) ld::core::renderPaths(*paths_, out, sceneToImage, pathOptions_);
    }
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!ncMode_ && !imageItem_->pixmap().isNull()) {
        // das Bild so, wie es auf der Arbeitsfläche liegt (deckende Farben, durchsichtiges Weiß auf dem Papier)
        painter.setTransform(imageItem_->sceneTransform() * sceneToImage);
        painter.drawPixmap(0, 0, imageItem_->pixmap());
        painter.resetTransform();
    }
    // der schon gebrannte Teil in der Fortschrittsfarbe; auf dem kleinen Bildschirm etwas deckender als am PC
    painter.setOpacity(0.7);
    painter.drawImage(QRectF(0, 0, out.width(), out.height()), overlay_.image());
    return out;
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    if (resizing_) {
        doResize(event->pos());
    } else if (moving_) {
        doMove(event->pos());
    } else if (panning_) {
        const QPoint delta = event->pos() - lastPanPos_;
        lastPanPos_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    } else {
        QGraphicsView::mouseMoveEvent(event);
        updateHoverCursor(event->pos());
    }
    // Die Lineale zeigen die Mausposition als Strich.
    mousePos_ = event->pos();
    mouseInside_ = true;
    if (horizontalRuler_) horizontalRuler_->update();
    if (verticalRuler_) verticalRuler_->update();
    if (!hasContent()) {
        emit mouseMoved(false, QPointF());
        return;
    }
    emit mouseMoved(true, rulerValueAt(mapToScene(event->pos())));   // Bemaßung ab dem Job-Ursprung
}

void CanvasView::leaveEvent(QEvent* event)
{
    QGraphicsView::leaveEvent(event);
    mouseInside_ = false;
    if (horizontalRuler_) horizontalRuler_->update();
    if (verticalRuler_) verticalRuler_->update();
    emit mouseMoved(false, QPointF());
}

void CanvasView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (!hasContent()) {
        // Ohne Bild: Hinweistext in der Mitte der sichtbaren Fläche (in Bildschirmkoordinaten).
        painter->save();
        painter->resetTransform();
        painter->setPen(palette().color(QPalette::PlaceholderText));
        painter->drawText(viewport()->rect(), Qt::AlignCenter | Qt::TextWordWrap,
                          tr("Noch kein Bild geladen.\n\n"
                             "Bild laden (Strg+O), hierher ziehen oder aus der Zwischenablage einfügen (Strg+V)."));
        painter->restore();
        return;
    }

    // 1) Laserwege (Bildschirmauflösung; ein älteres Bild wird auf den heutigen Ausschnitt gerechnet).
    if (!pathImage_.isNull()) {
        painter->save();
        painter->resetTransform();
        painter->drawImage(mapFromScene(pathImageRect_).boundingRect(), pathImage_);
        painter->restore();
    }

    // 2) Grüner Fortschritt der Simulation (in Szenenkoordinaten).
    if (overlay_.isActive()) {
        painter->save();
        painter->setOpacity(ProgressOverlay::kOpacity);
        painter->drawImage(overlay_.rect(), overlay_.image());
        painter->restore();
    }

    // 3) Markierungen in Bildschirmgröße: Nullpunkt mit Achsenpfeilen, Kopf der Simulation.
    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing);

    const QPointF origin = mapFromScene(jobOriginScenePoint());
    const QColor accent(0xd4, 0x2a, 0x29);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(accent, 2));
    painter->drawEllipse(origin, 7.0, 7.0);
    painter->drawLine(origin + QPointF(-12, 0), origin + QPointF(12, 0));
    painter->drawLine(origin + QPointF(0, -12), origin + QPointF(0, 12));

    // Achsenpfeile: Sie zeigen in die Zählrichtung der Lineale, also je nach Job-Ursprung ins Bild hinein (planung.md 3.23).
    struct Axis { QPointF direction; QColor color; const char* name; };
    const QPointF sign = rulerSign();   // +1 = nach rechts bzw. nach unten
    const Axis axes[] = {{QPointF(sign.x(), 0), QColor(0xd4, 0x2a, 0x29), "X"}, {QPointF(0, sign.y()), QColor(0x2a, 0x6f, 0xd4), "Y"}};
    for (const Axis& axis : axes) {
        const QPointF dir = axis.direction;   // Bildschirm: Y nach unten
        const QPointF tip = origin + dir * 44.0;
        painter->setPen(QPen(axis.color, 2));
        painter->drawLine(origin + dir * 8.0, tip);
        // Pfeilspitze
        const QPointF side(-dir.y(), dir.x());
        QPainterPath head;
        head.moveTo(tip + dir * 6.0);
        head.lineTo(tip - dir * 4.0 + side * 4.5);
        head.lineTo(tip - dir * 4.0 - side * 4.5);
        head.closeSubpath();
        painter->setBrush(axis.color);
        painter->drawPath(head);
        painter->setBrush(Qt::NoBrush);
        painter->drawText(tip + dir * 12.0 + QPointF(-4, 5), QString::fromLatin1(axis.name));
    }

    if (head_) {
        const QPointF h = mapFromScene(*head_);
        painter->setPen(QPen(QColor(0x2e, 0x9e, 0x4f), 2));
        painter->drawEllipse(h, 5.0, 5.0);
        painter->drawLine(h + QPointF(-11, 0), h + QPointF(11, 0));
        painter->drawLine(h + QPointF(0, -11), h + QPointF(0, 11));
    }

    // 4) Hinweis zu den Laserwegen ("wird erzeugt …", Fehler).
    if (!pathMessage_.isEmpty()) {
        const QRect box = viewport()->rect().adjusted(40, 10, -40, 0);
        painter->setPen(palette().color(QPalette::WindowText));
        QRect text = painter->boundingRect(box, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, pathMessage_);
        painter->fillRect(text.adjusted(-8, -4, 8, 4), palette().color(QPalette::Window));
        painter->drawText(box, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, pathMessage_);
    }

    // 5) Anfasser am blauen Rahmen (in Bildschirmkoordinaten).
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(0x2f, 0x80, 0xff), 1));
    painter->setBrush(palette().color(QPalette::Base));
    for (const Handle& handle : handles()) {
        painter->drawRect(QRect(handle.pos.x() - 4, handle.pos.y() - 4, 8, 8));
    }
    painter->restore();
}

// ---------------------------------------------------------------------------------------------
// Anfasser: Größe der Grafik mit der Maus ändern
// ---------------------------------------------------------------------------------------------
namespace {
// Richtung der Anfasser (0 = links oben, dann im Uhrzeigersinn): x/y = -1, 0 oder +1.
constexpr int kHandleDx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
constexpr int kHandleDy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
}  // namespace

void CanvasView::setResizeEnabled(bool enabled)
{
    resizeEnabled_ = enabled;
    viewport()->update();
}

bool CanvasView::axisAligned() const
{
    const double rest = std::fmod(std::abs(document_->transform().rotationDeg), 90.0);
    return rest < 0.01 || rest > 89.99;
}

QList<CanvasView::Handle> CanvasView::handles() const
{
    QList<Handle> list;
    if (!resizeEnabled_ || ncMode_ || !document_->hasImage() || !boundsItem_->isVisible()) {
        return list;
    }
    const bool aligned = axisAligned();
    if (!aligned) {
        // Schräg gedreht: nur die vier Ecken des gedrehten Rahmens (gleichmäßig skalieren).
        for (int id = 0; id < 8; id += 2) {
            list.append({id, mapFromScene(rotatedCorner(kHandleDx[id], kHandleDy[id]))});
        }
        return list;
    }
    const QRect r = mapFromScene(boundsItem_->rect()).boundingRect();
    for (int id = 0; id < 8; ++id) {
        const int x = r.center().x() + kHandleDx[id] * (r.width() / 2);
        const int y = r.center().y() + kHandleDy[id] * (r.height() / 2);
        list.append({id, QPoint(x, y)});
    }
    return list;
}

int CanvasView::handleAt(const QPoint& pos) const
{
    for (const Handle& handle : handles()) {
        if (std::abs(handle.pos.x() - pos.x()) <= 6 && std::abs(handle.pos.y() - pos.y()) <= 6) {
            return handle.id;
        }
    }
    return -1;
}

void CanvasView::updateHoverCursor(const QPoint& mouse)
{
    if (panning_ || resizing_ || moving_) return;
    const int handle = handleAt(mouse);
    if (handle < 0) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    const int dx = kHandleDx[handle], dy = kHandleDy[handle];
    if (dx == 0) viewport()->setCursor(Qt::SizeVerCursor);
    else if (dy == 0) viewport()->setCursor(Qt::SizeHorCursor);
    else viewport()->setCursor(dx * dy > 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
}

bool CanvasView::insideGraphic(const QPoint& pos) const
{
    if (!resizeEnabled_ || ncMode_ || !document_->hasImage() || !frameItem_->isVisible()) return false;
    return frameItem_->contains(frameItem_->mapFromScene(mapToScene(pos)));
}

void CanvasView::startMove(const QPoint& mouse)
{
    moving_ = true;
    moveStartMouse_ = mouse;
    moveStartOffset_ = QPointF(document_->transform().offsetXMm, document_->transform().offsetYMm);
    viewport()->setCursor(Qt::SizeAllCursor);
}

void CanvasView::doMove(const QPoint& mouse)
{
    const double ppm = pixelsPerMm();
    if (ppm <= 0.0) return;
    // Die Grafik folgt der Maus; der Nullpunkt bleibt auf dem Bildschirm stehen.
    const QPointF target = moveStartOffset_ + QPointF(mouse - moveStartMouse_) / ppm;
    document_->previewOffsetMm(target);   // wirkt sofort, der Undo-Schritt entsteht erst beim Loslassen
}

void CanvasView::endMove()
{
    moving_ = false;
    document_->commitOffsetMm(moveStartOffset_);   // ein Undo-Schritt für das ganze Ziehen
    viewport()->setCursor(Qt::CrossCursor);
}

QPointF CanvasView::rotatedCorner(int dx, int dy) const
{
    const ld::core::ObjectTransform& t = document_->transform();
    QTransform rotation;
    rotation.rotate(t.rotationDeg);
    return rotation.map(QPointF(dx * t.widthMm / 2.0, dy * t.heightMm / 2.0));
}

void CanvasView::startResize(int handle, const QPoint& mouse)
{
    Q_UNUSED(mouse);
    resizing_ = true;
    resizeMacroOpen_ = false;
    resizeHandle_ = handle;
    resizeStart_ = document_->transform();
    resizeStartBounds_ = ld::core::boundingSize(resizeStart_);
    // Fester Punkt: die Gegenseite des Anfassers (bei Kantenanfassern die Mitte der anderen Achse).
    if (!axisAligned()) {
        // Schräg gedreht: Gegenecke des gedrehten Rahmens ist fest, die Größe folgt dem Abstand zu ihr.
        resizeAnchorView_ = mapFromScene(rotatedCorner(-kHandleDx[handle], -kHandleDy[handle]));
        resizeCornerView_ = mapFromScene(rotatedCorner(kHandleDx[handle], kHandleDy[handle]));
    } else {
        const QPointF anchorScene(-kHandleDx[handle] * resizeStartBounds_.width() / 2.0,
                                  -kHandleDy[handle] * resizeStartBounds_.height() / 2.0);
        resizeAnchorView_ = mapFromScene(anchorScene);
    }
    viewport()->setCursor(viewport()->cursor());   // der Größen-Zeiger bleibt während des Ziehens
}

void CanvasView::doResize(const QPoint& mouse)
{
    const double ppm = pixelsPerMm();
    if (ppm <= 0.0 || resizeStartBounds_.width() <= 0.0 || resizeStartBounds_.height() <= 0.0) return;
    const int dx = kHandleDx[resizeHandle_], dy = kHandleDy[resizeHandle_];
    if (!axisAligned()) {
        // Schräg gedreht: Faktor aus der Lage der Maus auf der Diagonale (feste Gegenecke -> gezogene Ecke).
        const QPointF diagonal = QPointF(resizeCornerView_ - resizeAnchorView_);
        const double lengthSquared = diagonal.x() * diagonal.x() + diagonal.y() * diagonal.y();
        if (lengthSquared <= 0.0) return;
        const QPointF toMouse = QPointF(mouse - resizeAnchorView_);
        const double s = std::max(0.001, (toMouse.x() * diagonal.x() + toMouse.y() * diagonal.y()) / lengthSquared);
        double w = resizeStart_.widthMm * s;
        double h = resizeStart_.heightMm * s;
        const double minimum = 0.5;
        if (w < minimum || h < minimum) {
            const double k = std::max(minimum / w, minimum / h);
            w *= k;
            h *= k;
        }
        if (!resizeMacroOpen_ && (std::abs(w - document_->transform().widthMm) > 1e-9 || std::abs(h - document_->transform().heightMm) > 1e-9)) {
            document_->undoStack()->beginMacro(tr("Größe ändern"));
            resizeMacroOpen_ = true;
        }
        document_->setSizeMm(w, h);
        const QPoint now = mapFromScene(rotatedCorner(-dx, -dy));
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (now.x() - resizeAnchorView_.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + (now.y() - resizeAnchorView_.y()));
        return;
    }
    // Neue Ausdehnung des umschließenden Rechtecks, gemessen von der festen Gegenseite aus (mindestens 1 mm).
    const double newW = dx != 0 ? std::max(1.0, dx * (mouse.x() - resizeAnchorView_.x()) / ppm) : resizeStartBounds_.width();
    const double newH = dy != 0 ? std::max(1.0, dy * (mouse.y() - resizeAnchorView_.y()) / ppm) : resizeStartBounds_.height();
    double sx = newW / resizeStartBounds_.width();
    double sy = newH / resizeStartBounds_.height();
    // Gleichmäßig skalieren (Seitenverhältnis bleibt), wenn der Haken gesetzt ist oder die Grafik schräg gedreht ist.
    if (keepAspect_ || !axisAligned()) {
        double s = sx;
        if (dx != 0 && dy != 0) s = std::abs(sx - 1.0) >= std::abs(sy - 1.0) ? sx : sy;   // Ecke: die größere Änderung gilt
        else if (dy != 0) s = sy;
        sx = sy = s;
    }
    // Das umschließende Rechteck gehört zu Breite/Höhe der Grafik; bei 90° und 270° sind die Achsen vertauscht.
    const double rest = std::fmod(std::abs(resizeStart_.rotationDeg), 180.0);
    const bool swapped = rest > 45.0 && rest < 135.0;
    double w = resizeStart_.widthMm * (swapped ? sy : sx);
    double h = resizeStart_.heightMm * (swapped ? sx : sy);
    const double minimum = 0.5;
    if (w < minimum || h < minimum) {
        const double k = std::max(minimum / w, minimum / h);   // nie kleiner als 0,5 mm
        w *= k;
        h *= k;
    }
    // Das ganze Ziehen ist ein Undo-Schritt (der Schritt wird erst bei der ersten echten Änderung begonnen).
    if (!resizeMacroOpen_ && (std::abs(w - document_->transform().widthMm) > 1e-9 || std::abs(h - document_->transform().heightMm) > 1e-9)) {
        document_->undoStack()->beginMacro(tr("Größe ändern"));
        resizeMacroOpen_ = true;
    }
    document_->setSizeMm(w, h);

    // Die Szene ist an der Bildmitte verankert: Damit die Gegenseite stehen bleibt, wird die Ansicht nachgeschoben.
    const QSizeF bounds = ld::core::boundingSize(document_->transform());
    const QPointF anchorScene(-dx * bounds.width() / 2.0, -dy * bounds.height() / 2.0);
    const QPoint now = mapFromScene(anchorScene);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (now.x() - resizeAnchorView_.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + (now.y() - resizeAnchorView_.y()));
}

void CanvasView::endResize()
{
    if (resizeMacroOpen_) {
        document_->undoStack()->endMacro();
    }
    resizeMacroOpen_ = false;
    resizing_ = false;
    resizeHandle_ = -1;
}

}  // namespace ld::app
