#include "core/pathdata.h"

#include <QFile>
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/gcodereader.h"
#include "core/movetimer.h"

namespace ld::core {

QPointF machineToScene(const MachineSetup& setup, JobOrigin origin, const QSizeF& boundsMm,
                       const QPointF& machine, const QPointF& offsetMm)
{
    const QPointF opUp = machineToOperatorCorrected(setup, machine);   // relativ zum Job-Ursprung, Y nach oben
    const QPointF fraction = jobOriginFraction(origin);
    const QPointF originPoint(-boundsMm.width() / 2.0 + fraction.x() * boundsMm.width() - offsetMm.x(),
                              -boundsMm.height() / 2.0 + fraction.y() * boundsMm.height() - offsetMm.y());
    return QPointF(opUp.x() + originPoint.x(), -opUp.y() + originPoint.y());
}

std::shared_ptr<PathData> PathData::load(const QString& file, const GcodeJob& job,
                                         const std::atomic_bool* cancel,
                                         const std::function<void(double)>& progress,
                                         size_t maxSegments)
{
    QFile device(file);
    if (!device.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    auto data = std::make_shared<PathData>();
    const QSizeF bounds = boundingSize(job.transform);

    // Grobe Schätzung der Zahl der Fahrten aus der Dateigröße (eine Zeile hat meist 12 bis 30 Byte).
    const qint64 fileSize = device.size();
    const double expectedMoves = static_cast<double>(fileSize) / 12.0;
    data->lineStride = std::max(1, static_cast<int>(std::ceil(expectedMoves / static_cast<double>(maxSegments))));
    data->coarse = data->lineStride > 1;

    GcodeReader reader(&device);
    GcodeMove move;
    double time = 0.0;
    MoveTimer timer(job.machine.rapidMmPerMin, job.machine.accelerationX, job.machine.accelerationY);
    long long lineIndex = 0;   // Jede Zeile des Rasters beginnt mit einem Eilgang.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    const double rapidMmPerSecond = job.machine.rapidMmPerMin / 60.0;

    while (reader.next(move)) {
        ++data->fileMoves;
        if ((data->fileMoves & 0x3FFF) == 0) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return nullptr;
            }
            if (progress && fileSize > 0) {
                progress(static_cast<double>(device.pos()) / static_cast<double>(fileSize));
            }
        }
        const double length = std::hypot(move.to.x() - move.from.x(), move.to.y() - move.from.y());
        const double speed = (move.rapid || move.feedMmPerMin <= 0.0) ? rapidMmPerSecond : move.feedMmPerMin / 60.0;
        const double duration = length / speed;
        const double start = time;
        time += duration;
        timer.add(move.from, move.to, move.rapid, move.feedMmPerMin);
        if (move.rapid) {
            ++lineIndex;
        }
        const SegmentKind kind = move.rapid ? SegmentKind::Rapid
                                            : (move.power > 0 ? SegmentKind::Burn : SegmentKind::Travel);
        if (kind == SegmentKind::Rapid) data->rapidLengthMm += length;
        else if (kind == SegmentKind::Travel) data->travelLengthMm += length;
        else data->burnLengthMm += length;

        if (data->coarse && ((lineIndex - 1) % data->lineStride) != 0) {
            continue;   // diese Zeile wird ausgelassen (die Zeit läuft trotzdem weiter)
        }
        const QPointF offset(job.transform.offsetXMm, job.transform.offsetYMm);
        const QPointF a = machineToScene(job.machine, job.origin, bounds, move.from, offset);
        const QPointF b = machineToScene(job.machine, job.origin, bounds, move.to, offset);
        data->segments.push_back({static_cast<float>(a.x()), static_cast<float>(a.y()),
                                  static_cast<float>(b.x()), static_cast<float>(b.y()), kind});
        data->startTimes.push_back(static_cast<float>(start));
        data->endTimes.push_back(static_cast<float>(time));
        minX = std::min({minX, a.x(), b.x()});
        maxX = std::max({maxX, a.x(), b.x()});
        minY = std::min({minY, a.y(), b.y()});
        maxY = std::max({maxY, a.y(), b.y()});
    }
    // Mit Beschleunigung ist die Gesamtzeit länger als nach dem einfachen Modell. Alle Zeiten werden
    // gleichmäßig so gestreckt, dass die Simulation genauso lange läuft wie die Schätzung.
    data->totalSeconds = timer.seconds();
    if (timer.simpleSeconds() > 0.0 && std::abs(data->totalSeconds - timer.simpleSeconds()) > 1e-9) {
        const float factor = static_cast<float>(data->totalSeconds / timer.simpleSeconds());
        for (float& t : data->startTimes) t *= factor;
        for (float& t : data->endTimes) t *= factor;
    }
    if (!data->segments.empty()) {
        data->bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }
    data->segments.shrink_to_fit();
    data->startTimes.shrink_to_fit();
    data->endTimes.shrink_to_fit();
    if (progress) {
        progress(1.0);
    }
    return data;
}

size_t PathData::indexAt(double seconds) const
{
    const auto it = std::upper_bound(endTimes.begin(), endTimes.end(), static_cast<float>(seconds));
    return static_cast<size_t>(it - endTimes.begin());
}

QPointF PathData::positionAt(double seconds) const
{
    if (segments.empty()) {
        return {};
    }
    const size_t index = indexAt(seconds);
    if (index >= segments.size()) {
        const PathSegment& last = segments.back();
        return QPointF(last.x1, last.y1);
    }
    const PathSegment& s = segments[index];
    const double span = static_cast<double>(endTimes[index]) - startTimes[index];
    const double f = span > 0.0 ? std::clamp((seconds - startTimes[index]) / span, 0.0, 1.0) : 1.0;
    return QPointF(s.x0 + (s.x1 - s.x0) * f, s.y0 + (s.y1 - s.y0) * f);
}

size_t locateOnPath(const PathData& data, size_t from, const QPointF& position, size_t window, double* distanceOut)
{
    const size_t count = data.segments.size();
    size_t best = std::min(from, count);
    double bestDist = std::numeric_limits<double>::max();
    const size_t end = std::min(count, from + window);
    for (size_t i = from; i < end; ++i) {
        const PathSegment& s = data.segments[i];
        const double dx = s.x1 - s.x0, dy = s.y1 - s.y0;
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0.0 ? ((position.x() - s.x0) * dx + (position.y() - s.y0) * dy) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double px = s.x0 + t * dx, py = s.y0 + t * dy;
        const double d = std::hypot(position.x() - px, position.y() - py);
        if (d < bestDist - 1e-3) {   // erst ab 1 µm Unterschied "besser": bei Gleichstand gilt das frühere Wegstück
            bestDist = d;
            best = i;
        }
    }
    if (distanceOut) *distanceOut = bestDist;
    return best;
}

}  // namespace ld::core
