#include "core/vectorize.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include "potracelib.h"
}

namespace ld::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

// Abstand des Punktes p von der Geraden durch a und b.
double distanceToLine(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double length = std::hypot(dx, dy);
    if (length < 1e-12) {
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    }
    return std::abs((p.x() - a.x()) * dy - (p.y() - a.y()) * dx) / length;
}

// Zerlegt eine Bézierkurve in Strecken: Teilt so lange in der Mitte (de Casteljau), bis die
// Kontrollpunkte höchstens tolerance von der Sehne entfernt sind. Der Startpunkt p0 wird nicht
// ausgegeben, der Endpunkt p3 schon.
void flattenBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3,
                   double tolerance, int depth, std::vector<QPointF>& out)
{
    if (depth >= 16 || std::max(distanceToLine(p1, p0, p3), distanceToLine(p2, p0, p3)) <= tolerance) {
        out.push_back(p3);
        return;
    }
    const QPointF p01 = (p0 + p1) / 2.0, p12 = (p1 + p2) / 2.0, p23 = (p2 + p3) / 2.0;
    const QPointF p012 = (p01 + p12) / 2.0, p123 = (p12 + p23) / 2.0;
    const QPointF mid = (p012 + p123) / 2.0;
    flattenBezier(p0, p01, p012, mid, tolerance, depth + 1, out);
    flattenBezier(mid, p123, p23, p3, tolerance, depth + 1, out);
}

// Gibt die potrace-Struktur beim Verlassen des Gültigkeitsbereichs frei.
struct PotraceCleanup {
    potrace_param_t* param = nullptr;
    potrace_state_t* state = nullptr;
    ~PotraceCleanup()
    {
        if (state) potrace_state_free(state);
        if (param) potrace_param_free(param);
    }
};

// Wandelt einen potrace-Pfad (Kurven) in einen Streckenzug in Bildkoordinaten (y nach unten).
Contour contourFromPath(const potrace_path_t* path, int height, double tolerancePx)
{
    Contour contour;
    const potrace_curve_t& curve = path->curve;
    auto toImage = [height](const potrace_dpoint_t& p) { return QPointF(p.x, height - p.y); };
    QPointF start = toImage(curve.c[curve.n - 1][2]);
    std::vector<QPointF>& pts = contour.points;
    for (int i = 0; i < curve.n; ++i) {
        if (curve.tag[i] == POTRACE_CORNER) {
            pts.push_back(toImage(curve.c[i][1]));
            pts.push_back(toImage(curve.c[i][2]));
        } else {
            flattenBezier(start, toImage(curve.c[i][0]), toImage(curve.c[i][1]), toImage(curve.c[i][2]),
                          tolerancePx, 0, pts);
        }
        start = toImage(curve.c[i][2]);
    }
    // Doppelte Punkte (auch der Schlusspunkt gleich dem ersten) entfernen.
    std::vector<QPointF> clean;
    for (const QPointF& p : pts) {
        if (clean.empty() || std::hypot(p.x() - clean.back().x(), p.y() - clean.back().y()) > 1e-9) {
            clean.push_back(p);
        }
    }
    while (clean.size() > 1 && std::hypot(clean.front().x() - clean.back().x(),
                                          clean.front().y() - clean.back().y()) <= 1e-9) {
        clean.pop_back();
    }
    pts = std::move(clean);
    return contour;
}

// Läuft den potrace-Baum ab (Eltern vor Kindern) und füllt die Liste.
void collect(const potrace_path_t* first, int parent, int depth, int height, double tolerancePx,
             std::vector<Contour>& out)
{
    for (const potrace_path_t* path = first; path; path = path->sibling) {
        Contour contour = contourFromPath(path, height, tolerancePx);
        contour.parent = parent;
        contour.depth = depth;
        const int index = static_cast<int>(out.size());
        const bool usable = contour.points.size() >= 3;
        if (usable) {
            out.push_back(std::move(contour));
        }
        // Wurde der Umriss verworfen, rücken die Kinder zum Großelternteil auf (Tiefe bleibt gerade/ungerade).
        collect(path->childlist, usable ? index : parent, depth + 1, height, tolerancePx, out);
    }
}

}  // namespace

namespace {

double ringArea(const std::vector<QPointF>& pts)
{
    double sum = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const QPointF& a = pts[i];
        const QPointF& b = pts[(i + 1) % pts.size()];
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(sum) / 2.0;
}

bool pointInRing(const QPointF& p, const std::vector<QPointF>& ring)
{
    bool inside = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const QPointF& a = ring[i];
        const QPointF& b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y()) &&
            p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
            inside = !inside;
        }
    }
    return inside;
}

}  // namespace

void assignNesting(std::vector<Contour>& contours)
{
    constexpr size_t kMaxNested = 2000;
    std::vector<int> closed;
    for (size_t i = 0; i < contours.size(); ++i) {
        contours[i].parent = -1;
        contours[i].depth = 0;
        if (contours[i].closed && contours[i].points.size() >= 3) {
            closed.push_back(static_cast<int>(i));
        }
    }
    if (closed.size() > kMaxNested) {
        return;
    }
    std::vector<double> area(contours.size(), 0.0);
    for (const int i : closed) {
        area[static_cast<size_t>(i)] = ringArea(contours[static_cast<size_t>(i)].points);
    }
    // Große zuerst: Der Elternteil ist immer größer als das Kind, seine Tiefe also schon bekannt.
    std::sort(closed.begin(), closed.end(), [&](int a, int b) { return area[static_cast<size_t>(a)] > area[static_cast<size_t>(b)]; });
    for (size_t k = 0; k < closed.size(); ++k) {
        Contour& child = contours[static_cast<size_t>(closed[k])];
        int best = -1;
        for (size_t m = k; m-- > 0;) {   // von den nächstkleineren Kandidaten rückwärts
            const Contour& cand = contours[static_cast<size_t>(closed[m])];
            if (area[static_cast<size_t>(closed[m])] > area[static_cast<size_t>(closed[k])] &&
                pointInRing(child.points.front(), cand.points)) {
                best = closed[m];
                break;
            }
        }
        if (best >= 0) {
            child.parent = best;
            child.depth = contours[static_cast<size_t>(best)].depth + 1;
        }
    }
}

VectorizeResult vectorizeRaster(const QImage& raster, const VectorizeSettings& settings, double toleranceMm,
                                double pixelMm, const std::atomic_bool* cancel)
{
    VectorizeResult result;
    if (raster.isNull() || raster.format() != QImage::Format_Grayscale8 || pixelMm <= 0.0) {
        result.error = tr("Es gibt noch kein berechnetes Bild.");
        return result;
    }
    const int w = raster.width();
    const int h = raster.height();

    // potrace-Bitmap: Ein Bit je Pixel, das linke Pixel ist das höchste Bit; Zeile 0 ist UNTEN.
    constexpr int kWordBits = sizeof(potrace_word) * 8;
    potrace_bitmap_t bitmap;
    bitmap.w = w;
    bitmap.h = h;
    bitmap.dy = (w + kWordBits - 1) / kWordBits;
    std::vector<potrace_word> words(static_cast<size_t>(bitmap.dy) * h, 0);
    bitmap.map = words.data();
    for (int y = 0; y < h; ++y) {
        const uchar* line = raster.constScanLine(y);
        potrace_word* row = words.data() + static_cast<size_t>(h - 1 - y) * bitmap.dy;
        for (int x = 0; x < w; ++x) {
            if (line[x] < 128) {
                row[x / kWordBits] |= static_cast<potrace_word>(1) << (kWordBits - 1 - x % kWordBits);
            }
        }
        if (cancel && (y % 256) == 0 && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
    }

    PotraceCleanup guard;
    guard.param = potrace_param_default();
    if (!guard.param) {
        result.error = tr("potrace konnte nicht gestartet werden.");
        return result;
    }
    guard.param->turdsize = std::clamp(settings.turdSize, 0, 1000);
    guard.param->alphamax = std::clamp(settings.alphaMax, 0.0, 1.3333);
    guard.param->opticurve = settings.optTolerance > 0.0 ? 1 : 0;
    guard.param->opttolerance = std::max(0.0, settings.optTolerance);
    guard.state = potrace_trace(guard.param, &bitmap);
    if (!guard.state || guard.state->status != POTRACE_STATUS_OK) {
        result.error = tr("Die Vektorisierung ist fehlgeschlagen.");
        return result;
    }
    if (cancel && cancel->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }
    collect(guard.state->plist, -1, 0, h, std::max(0.001, toleranceMm / pixelMm), result.contours);
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------------------------
// Pixelgenaue Flächen
// ---------------------------------------------------------------------------------------------
namespace {

// Jede Kante eines Pixels hat eine Richtung im Uhrzeigersinn (Bildschirm, y nach unten), sodass die
// Fläche rechts der Fahrtrichtung liegt: 0 = oben (nach rechts), 1 = rechts (nach unten),
// 2 = unten (nach links), 3 = links (nach oben). Die Richtungsnummer ist zugleich die Nummer der Kante.
constexpr int kDirX[4] = {1, 0, -1, 0};
constexpr int kDirY[4] = {0, 1, 0, -1};

}  // namespace

VectorizeResult vectorizePixelAreas(const QImage& raster, const std::array<int, 256>& powerOfGray,
                                    const std::atomic_bool* cancel)
{
    VectorizeResult result;
    if (raster.isNull() || raster.format() != QImage::Format_Grayscale8) {
        result.error = tr("Es gibt noch kein berechnetes Bild.");
        return result;
    }
    const int w = raster.width(), h = raster.height();
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    auto index = [&](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); };

    // 1) Flächen bestimmen: gleiche Leistung (größer 0) und gemeinsame Kante.
    std::vector<int> label(count, 0);
    std::vector<int> labelPower(1, 0);   // Leistung je Fläche (Index 0 unbenutzt)
    {
        std::vector<int> stack;
        for (int y = 0; y < h; ++y) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                result.cancelled = true;
                return result;
            }
            const uchar* row = raster.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                const int power = powerOfGray[row[x]];
                if (power <= 0 || label[index(x, y)] != 0) continue;
                const int id = static_cast<int>(labelPower.size());
                labelPower.push_back(power);
                label[index(x, y)] = id;
                stack.push_back(static_cast<int>(index(x, y)));
                while (!stack.empty()) {
                    const int cur = stack.back();
                    stack.pop_back();
                    const int cx = cur % w, cy = cur / w;
                    for (int d = 0; d < 4; ++d) {
                        const int nx = cx + kDirX[d], ny = cy + kDirY[d];
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        if (label[index(nx, ny)] != 0) continue;
                        if (powerOfGray[raster.constScanLine(ny)[nx]] != power) continue;
                        label[index(nx, ny)] = id;
                        stack.push_back(static_cast<int>(index(nx, ny)));
                    }
                }
            }
        }
    }

    // 2) Umrisse: Kanten zwischen einer Fläche und ihrer Umgebung zu geschlossenen Schleifen verketten.
    // Eine Kante ist (Pixel, Seite) mit der Fläche rechts der Fahrtrichtung. An Ecken, an denen mehrere
    // Kanten derselben Fläche weitergehen, wird zuerst rechts abgebogen (enge Kurve um das Pixel): So
    // bleiben Pixel, die sich nur an der Ecke berühren, getrennt (Nachbarschaft über Kanten).
    auto labelAt = [&](int x, int y) -> int {
        return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : label[index(x, y)];
    };
    // Das Pixel jenseits der Seite s des Pixels (x, y).
    auto beyond = [&](int x, int y, int s) { return labelAt(x + (s == 1) - (s == 3), y + (s == 2) - (s == 0)); };
    auto edgeExists = [&](int x, int y, int s, int L) { return labelAt(x, y) == L && beyond(x, y, s) != L; };
    // Die Kante, die am Eckpunkt (vx, vy) in Richtung d beginnt, gehört zu diesem Pixel (Seite = d).
    auto ownerOf = [](int vx, int vy, int d, int& px, int& py) {
        switch (d) {
        case 0: px = vx; py = vy; break;           // oben: das Pixel rechts unterhalb des Punktes
        case 1: px = vx - 1; py = vy; break;       // rechts: das Pixel links unterhalb
        case 2: px = vx - 1; py = vy - 1; break;   // unten: das Pixel links oberhalb
        default: px = vx; py = vy - 1; break;      // links: das Pixel rechts oberhalb
        }
    };
    std::vector<unsigned char> visited(count, 0);   // je Pixel ein Bit je Seite
    struct Loop {
        std::vector<QPointF> points;
        int label = 0;
        double area = 0.0;   // vorzeichenbehaftet: positiv = im Uhrzeigersinn (äußerer Umriss)
    };
    std::vector<Loop> loops;
    for (int y = 0; y < h; ++y) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        for (int x = 0; x < w; ++x) {
            const int L = labelAt(x, y);
            if (L == 0) continue;
            for (int s = 0; s < 4; ++s) {
                if ((visited[index(x, y)] & (1u << s)) || !edgeExists(x, y, s, L)) continue;
                // Neue Schleife ab dieser Kante: erst alle Kanten sammeln (Anfangspunkt, Richtung) ...
                std::vector<std::pair<QPointF, int>> edges;
                int px = x, py = y, side = s;
                while (true) {
                    visited[index(px, py)] |= static_cast<unsigned char>(1u << side);
                    int vx = px, vy = py;   // Anfangspunkt der Kante
                    switch (side) {
                    case 1: vx = px + 1; break;
                    case 2: vx = px + 1; vy = py + 1; break;
                    case 3: vy = py + 1; break;
                    default: break;
                    }
                    edges.emplace_back(QPointF(vx, vy), side);
                    // ... am Endpunkt weiter: rechts abbiegen, geradeaus oder links.
                    const int ex = vx + kDirX[side], ey = vy + kDirY[side];
                    int nextSide = -1, nx = 0, ny = 0;
                    for (const int turn : {1, 0, 3}) {
                        const int d = (side + turn) % 4;
                        ownerOf(ex, ey, d, nx, ny);
                        if (edgeExists(nx, ny, d, L)) {
                            nextSide = d;
                            break;
                        }
                    }
                    if (nextSide < 0) break;   // kann nicht vorkommen (die Schleife ist geschlossen)
                    px = nx;
                    py = ny;
                    side = nextSide;
                    if (visited[index(px, py)] & (1u << side)) break;   // zurück am Anfang
                }
                // ... dann nur die Ecken behalten (Richtungswechsel) und die Fläche bestimmen.
                Loop loop;
                loop.label = L;
                for (size_t k = 0; k < edges.size(); ++k) {
                    const int previous = edges[(k + edges.size() - 1) % edges.size()].second;
                    if (edges[k].second != previous) loop.points.push_back(edges[k].first);
                }
                double sum = 0.0;
                for (size_t k = 0; k < loop.points.size(); ++k) {
                    const QPointF& a = loop.points[k];
                    const QPointF& b = loop.points[(k + 1) % loop.points.size()];
                    sum += a.x() * b.y() - b.x() * a.y();
                }
                loop.area = sum / 2.0;
                if (loop.points.size() >= 4) loops.push_back(std::move(loop));
            }
        }
    }

    // 3) Äußere Schleifen zuerst, dann die Löcher als ihre Kinder (Eltern stehen vor den Kindern).
    std::vector<int> outerOf(labelPower.size(), -1);
    for (Loop& loop : loops) {
        if (loop.area <= 0.0) continue;
        outerOf[static_cast<size_t>(loop.label)] = static_cast<int>(result.contours.size());
        Contour contour;
        contour.points = std::move(loop.points);
        contour.depth = 0;
        contour.powerS = labelPower[static_cast<size_t>(loop.label)];
        result.contours.push_back(std::move(contour));
    }
    for (Loop& loop : loops) {
        if (loop.area > 0.0) continue;
        const int parent = outerOf[static_cast<size_t>(loop.label)];
        if (parent < 0) continue;
        Contour contour;
        contour.points = std::move(loop.points);
        contour.depth = 1;
        contour.parent = parent;
        contour.powerS = labelPower[static_cast<size_t>(loop.label)];
        result.contours.push_back(std::move(contour));
    }
    result.ok = true;
    return result;
}

}  // namespace ld::core
