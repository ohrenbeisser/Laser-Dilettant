#include "core/pathrender.h"

#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace ld::core {

bool renderPaths(const PathData& data, QImage& image, const QTransform& sceneToImage,
                 const PathRenderOptions& options, const std::atomic_bool* cancel)
{
    if (image.isNull() || data.segments.empty()) {
        return true;
    }
    bool invertible = false;
    const QTransform imageToScene = sceneToImage.inverted(&invertible);
    if (!invertible) {
        return true;
    }
    const double scale = std::max(1e-9, std::abs(sceneToImage.m11()));   // Bildpixel pro mm
    // Sichtbarer Ausschnitt der Szene mit etwas Rand (Liniendicke).
    const QRectF visible = imageToScene.mapRect(QRectF(image.rect())).adjusted(-1, -1, 1, 1);

    QPainter painter(&image);
    painter.setTransform(sceneToImage);
    // Weiche Kanten sind schöner, kosten aber Zeit: Bei sehr vielen Wegstücken darauf verzichten.
    painter.setRenderHint(QPainter::Antialiasing, data.segments.size() < 1'500'000);

    auto pass = [&](SegmentKind kind, const QPen& pen) {
        painter.setPen(pen);
        size_t counter = 0;
        for (const PathSegment& s : data.segments) {
            if ((++counter & 0x3FFF) == 0 && cancel && cancel->load(std::memory_order_relaxed)) {
                return false;
            }
            if (s.kind != kind) {
                continue;
            }
            // Wegstück komplett außerhalb des sichtbaren Ausschnitts: überspringen.
            if (std::max(s.x0, s.x1) < visible.left() || std::min(s.x0, s.x1) > visible.right() ||
                std::max(s.y0, s.y1) < visible.top() || std::min(s.y0, s.y1) > visible.bottom()) {
                continue;
            }
            painter.drawLine(QLineF(s.x0, s.y0, s.x1, s.y1));
        }
        return true;
    };

    // Reihenfolge: Eilgang unten, Gravur oben.
    if (options.showRapid) {
        QPen pen(options.rapidColor, 1.0, Qt::DashLine);
        pen.setCosmetic(true);
        if (!pass(SegmentKind::Rapid, pen)) return false;
    }
    if (options.showTravel) {
        QPen pen(options.travelColor, 1.0);
        pen.setCosmetic(true);
        if (!pass(SegmentKind::Travel, pen)) return false;
    }
    if (options.showBurn) {
        // Die Gravurspur ist so breit wie die Pixelgröße, mindestens aber ein Bildschirmpixel.
        QPen pen(options.burnColor, std::max(options.burnWidthMm, options.burnMinWidthPx / scale));
        pen.setCapStyle(Qt::FlatCap);
        if (!pass(SegmentKind::Burn, pen)) return false;
    }
    return true;
}

PathRenderRunner::PathRenderRunner(int debounceMs, QObject* parent) : QObject(parent)
{
    pool_.setMaxThreadCount(1);
    timer_.setSingleShot(true);
    timer_.setInterval(debounceMs);
    connect(&timer_, &QTimer::timeout, this, &PathRenderRunner::startJob);
}

PathRenderRunner::~PathRenderRunner()
{
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pool_.waitForDone();
}

void PathRenderRunner::request(std::shared_ptr<const PathData> data, const QSize& imageSize,
                               const QTransform& sceneToImage, const QRectF& sceneRect,
                               const PathRenderOptions& options)
{
    ++generation_;
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pending_ = {std::move(data), imageSize, sceneToImage, sceneRect, options};
    busy_ = true;
    timer_.start();
}

void PathRenderRunner::cancel()
{
    ++generation_;
    timer_.stop();
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    busy_ = false;
}

void PathRenderRunner::startJob()
{
    cancelFlag_ = std::make_shared<std::atomic_bool>(false);
    const quint64 generation = generation_;
    const auto flag = cancelFlag_;
    const Pending job = pending_;

    pool_.start([this, generation, flag, job]() {
        auto image = std::make_shared<QImage>(job.size, QImage::Format_ARGB32_Premultiplied);
        image->fill(Qt::transparent);
        const bool ok = job.data && renderPaths(*job.data, *image, job.transform, job.options, flag.get());
        QMetaObject::invokeMethod(
            this,
            [this, generation, image, ok, rect = job.sceneRect]() {
                if (generation != generation_) {
                    return;
                }
                busy_ = false;
                if (ok) {
                    emit finished(*image, rect);
                }
            },
            Qt::QueuedConnection);
    });
}

}  // namespace ld::core
