#include "core/rasterrunner.h"

#include <QMetaObject>

namespace ld::core {

RasterRunner::RasterRunner(int debounceMs, QObject* parent) : QObject(parent)
{
    pool_.setMaxThreadCount(1);  // höchstens eine Rechnung gleichzeitig
    timer_.setSingleShot(true);
    timer_.setInterval(debounceMs);
    connect(&timer_, &QTimer::timeout, this, &RasterRunner::startJob);
}

RasterRunner::~RasterRunner()
{
    // Laufende Rechnung abbrechen und abwarten, damit kein Thread mehr auf dieses Objekt zugreift.
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pool_.waitForDone();
}

void RasterRunner::request(const QImage& source, const ImageSettings& settings, const QSizeF& sizeMm,
                         std::shared_ptr<const SvgModel> svg)
{
    // Alles Vorherige ist überholt.
    ++generation_;
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pending_ = {source, settings, sizeMm, std::move(svg)};
    busy_ = true;
    emit started();
    timer_.start();  // startet (neu) die Wartezeit; danach startJob()
}

void RasterRunner::cancel()
{
    ++generation_;
    timer_.stop();
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    busy_ = false;
}

void RasterRunner::startJob()
{
    cancelFlag_ = std::make_shared<std::atomic_bool>(false);
    const quint64 generation = generation_;
    const auto flag = cancelFlag_;
    const Pending job = pending_;

    pool_.start([this, generation, flag, job]() {
        // Läuft im Hintergrundthread.
        QImage source = job.source;
        ImageSettings settings = job.settings;
        if (job.svg) {
            // SVG: Die Füllung direkt in Zielgröße zeichnen; ein Ausschnitt gibt es bei SVG nicht.
            source = renderSvgFill(*job.svg, settings.svg.fullStrokes, rasterSizeFor(job.sizeMm, settings.pixelSizeMm));
            settings.crop = QRect();
        }
        auto result = std::make_shared<ProcessResult>(processImage(source, settings, job.sizeMm, flag.get()));
        // Das Ergebnis zurück in den Thread dieses Objekts schicken.
        QMetaObject::invokeMethod(
            this,
            [this, generation, result]() {
                if (generation != generation_) {
                    return;  // überholt durch eine neuere Anfrage
                }
                busy_ = false;
                if (!result->cancelled) {
                    emit finished(*result);
                }
            },
            Qt::QueuedConnection);
    });
}

}  // namespace ld::core
