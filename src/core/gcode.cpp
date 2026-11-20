#include "core/gcode.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <functional>
#include <vector>

#include "core/gcodereader.h"
#include "core/movetimer.h"
#include "core/vectorize.h"

namespace ld::core {

QByteArray formatGcodeNumber(double value)
{
    QByteArray text = QByteArray::number(value, 'f', 3);
    if (text.contains('.')) {
        while (text.endsWith('0')) {
            text.chop(1);
        }
        if (text.endsWith('.')) {
            text.chop(1);
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

QString formatDuration(double seconds)
{
    const long long total = static_cast<long long>(std::llround(std::max(0.0, seconds)));
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

// Kommentare im G-Code nur mit einfachen ASCII-Zeichen (manche Sender mögen keine Umlaute),
// ohne Klammern und Semikolon.
QByteArray asciiComment(const QString& text)
{
    QByteArray out;
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        if (c == u'ä') out += "ae";
        else if (c == u'ö') out += "oe";
        else if (c == u'ü') out += "ue";
        else if (c == u'Ä') out += "Ae";
        else if (c == u'Ö') out += "Oe";
        else if (c == u'Ü') out += "Ue";
        else if (c == u'ß') out += "ss";
        else if (c == u'(' || c == u')' || c == u';') out += ' ';
        else if (c < 32 || c > 126) out += '?';
        else out += static_cast<char>(c);
    }
    return out;
}

QByteArray number1(double value)
{
    return QByteArray::number(value, 'f', 1);
}

// Sammelt Text und gibt ihn in größeren Stücken an die Senke weiter.
class Writer {
public:
    explicit Writer(const GcodeSink& sink) : sink_(sink) {}
    bool enabled() const { return static_cast<bool>(sink_); }
    long long lines() const { return lines_; }
    long long bytes() const { return bytes_; }

    bool line(const QByteArray& text)
    {
        ++lines_;
        bytes_ += text.size() + 1;
        if (!sink_) {
            return true;
        }
        buffer_ += text;
        buffer_ += '\n';
        return buffer_.size() < 64 * 1024 ? true : flush();
    }
    bool flush()
    {
        if (!sink_ || buffer_.isEmpty()) {
            return true;
        }
        const bool ok = sink_(buffer_);
        buffer_.clear();
        return ok;
    }

private:
    const GcodeSink& sink_;
    QByteArray buffer_;
    long long lines_ = 0;
    long long bytes_ = 0;
};

// Fährt Bewegungen aus: schreibt die Zeilen (modal, also nur Änderungen) und rechnet Strecke
// und Zeit mit. Positionen sind Maschinenkoordinaten in mm.
class Emitter {
public:
    Emitter(Writer& writer, GcodeStats& stats, double feedMmPerMin, const MachineSetup& machine)
        : writer_(writer), stats_(stats), feed_(feedMmPerMin),
          timer_(machine.rapidMmPerMin, machine.accelerationX, machine.accelerationY)
    {
    }

    bool move(bool rapid, const QPointF& to, int power)
    {
        const double length = std::hypot(to.x() - pos_.x(), to.y() - pos_.y());
        if (rapid) {
            stats_.travelLengthMm += length;
        } else {
            stats_.burnLengthMm += length;
        }
        timer_.add(pos_, to, rapid, feed_);
        pos_ = to;
        if (!writer_.enabled()) {
            return true;
        }

        QByteArray text = rapid ? "G0" : "G1";
        const QByteArray x = formatGcodeNumber(to.x());
        const QByteArray y = formatGcodeNumber(to.y());
        bool changed = false;
        if (x != lastX_) { text += " X" + x; lastX_ = x; changed = true; }
        if (y != lastY_) { text += " Y" + y; lastY_ = y; changed = true; }
        if (rapid) {
            // Der Eilgang schaltet den Laser im Lasermodus ohnehin ab; S0 ist ein zusätzlicher Schutz.
            if (lastPower_ != 0) { text += " S0"; lastPower_ = 0; changed = true; }
        } else {
            if (power != lastPower_) { text += " S" + QByteArray::number(power); lastPower_ = power; changed = true; }
            if (!feedSent_) { text += " F" + formatGcodeNumber(feed_); feedSent_ = true; changed = true; }
        }
        return changed ? writer_.line(text) : true;
    }

    QPointF position() const { return pos_; }

    // Verbucht eine Fahrt, die als Text vorgegeben ist (zum Beispiel aus dem Ende-Code):
    // nur Strecke und Zeit, geschrieben wird sie anderswo.
    void account(const GcodeMove& move)
    {
        const double length = std::hypot(move.to.x() - move.from.x(), move.to.y() - move.from.y());
        if (move.rapid || move.feedMmPerMin <= 0.0) {
            stats_.travelLengthMm += length;
        } else {
            stats_.burnLengthMm += length;
        }
        timer_.add(move.from, move.to, move.rapid, move.feedMmPerMin);
        pos_ = move.to;
    }

    // Gesamtzeit der bisher verbuchten Fahrten.
    double seconds() { return timer_.seconds(); }

private:
    Writer& writer_;
    GcodeStats& stats_;
    double feed_;
    MoveTimer timer_;
    QPointF pos_{0.0, 0.0};   // Der Auftrag beginnt am Nullpunkt der Maschine.
    QByteArray lastX_ = "0";
    QByteArray lastY_ = "0";
    int lastPower_ = 0;       // nach "M4 S0" bzw. "M3 S0"
    bool feedSent_ = false;
};

struct Run {
    int first;   // Zellenindex, einschließlich
    int last;    // Zellenindex, ausschließlich
    int power;   // S-Wert
};

// Schneidet die Gerade A + B*t mit dem Streifen |A + B*t| <= half; verengt [lo, hi].
bool clipSlab(double a, double b, double half, double& lo, double& hi)
{
    if (std::abs(b) < 1e-12) {
        return std::abs(a) <= half;
    }
    double t1 = (-half - a) / b;
    double t2 = (half - a) / b;
    if (t1 > t2) {
        std::swap(t1, t2);
    }
    lo = std::max(lo, t1);
    hi = std::min(hi, t2);
    return lo <= hi;
}


// ---------------------------------------------------------------------------------------------
// Full und Cutting: Bahnen aus den Umrissen (potrace). Alle Koordinaten im Koordinatensystem des
// Bedieners mit dem Mittelpunkt des Auftrags als Ursprung (X rechts, Y nach unten), in mm.
// ---------------------------------------------------------------------------------------------

using Ring = std::vector<QPointF>;

double sqr(double v) { return v * v; }

// Eine Gerade der Füllung: von t0 bis t1 auf der Zeile mit dem Querabstand nk.
struct FillLine {
    double t0, t1, nk;
    bool forward;
    int row = 0;               // Nummer der Zeile in der Fläche (benachbarte Zeilen haben aufeinanderfolgende Nummern)
    bool joinNext = false;     // Der Laser bleibt an und fährt ohne Absetzen in die nächste Zeile (Zickzack)
    int power = -1;            // Leistung dieser Fläche (S-Wert); -1 = die des Auftrags
};

// Ein Lauf: ein Stück einer Füllzeile innerhalb einer Fläche. Ein Balken ist eine Kette von Läufen in
// aufeinanderfolgenden Zeilen (siehe emitFull).
struct StripRun {
    double t0, t1, nk;
    int row;
};
struct Strip {
    std::vector<StripRun> runs;
    std::vector<int> topNb, bottomNb;   // angrenzende Balken oberhalb (an der ersten Zeile) und unterhalb (an der letzten)
    int power = -1;
    bool done = false;
};

// ---------------------------------------------------------------------------------------------
// Räumlicher Index für "nächster Punkt": ein Gitter, in dem Punkte (mit Kennzahl) liegen. Die Suche
// nach dem nächsten noch gültigen Punkt schaut ringförmig um die Anfrage herum und ist auch bei
// hunderttausenden Punkten schnell. Ungültige Einträge werden beim Suchen lazy entfernt.
// ---------------------------------------------------------------------------------------------
struct IndexEntry {
    QPointF p;
    int id;     // z. B. Nummer des Umrisses oder der Fläche
    int sub;    // z. B. Nummer des Punktes im Umriss oder Richtung
};

class PointIndex {
public:
    PointIndex(const QRectF& bounds, size_t expected)
    {
        const double w = std::max(bounds.width(), 1e-6), h = std::max(bounds.height(), 1e-6);
        // Etwa zwei Punkte je Zelle, höchstens 1024 Zellen je Richtung.
        double cell = std::sqrt(w * h / std::max<double>(1.0, expected / 2.0));
        cell = std::max(cell, std::max(w, h) / 1024.0);
        cell_ = std::max(cell, 1e-6);
        origin_ = bounds.topLeft();
        nx_ = std::clamp(static_cast<int>(w / cell_) + 1, 1, 1025);
        ny_ = std::clamp(static_cast<int>(h / cell_) + 1, 1, 1025);
        cells_.resize(static_cast<size_t>(nx_) * static_cast<size_t>(ny_));
    }

    void add(const QPointF& p, int id, int sub) { cells_[cellIndex(p)].push_back({p, id, sub}); }

    // Nächster Eintrag, für den alive(id) wahr ist. false, wenn es keinen mehr gibt.
    template <class Alive>
    bool nearest(const QPointF& q, Alive alive, IndexEntry& out)
    {
        const int cx = std::clamp(static_cast<int>((q.x() - origin_.x()) / cell_), 0, nx_ - 1);
        const int cy = std::clamp(static_cast<int>((q.y() - origin_.y()) / cell_), 0, ny_ - 1);
        double best = std::numeric_limits<double>::max();
        bool found = false;
        const int maxR = std::max(nx_, ny_);
        for (int r = 0; r <= maxR; ++r) {
            for (int y = cy - r; y <= cy + r; ++y) {
                if (y < 0 || y >= ny_) continue;
                const bool edgeRow = (y == cy - r || y == cy + r);
                const int step = edgeRow ? 1 : std::max(1, 2 * r);
                for (int x = cx - r; x <= cx + r; x += step) {
                    if (x < 0 || x >= nx_) continue;
                    auto& cell = cells_[static_cast<size_t>(y) * static_cast<size_t>(nx_) + static_cast<size_t>(x)];
                    for (size_t i = 0; i < cell.size();) {
                        if (!alive(cell[i].id)) {
                            cell[i] = cell.back();
                            cell.pop_back();
                            continue;
                        }
                        const double d = sqr(cell[i].p.x() - q.x()) + sqr(cell[i].p.y() - q.y());
                        if (d < best) {
                            best = d;
                            out = cell[i];
                            found = true;
                        }
                        ++i;
                    }
                }
            }
            // Alles, was in weiter außen liegenden Ringen liegt, ist mindestens r * cell_ entfernt.
            if (found && std::sqrt(best) <= r * cell_) break;
        }
        return found;
    }

private:
    size_t cellIndex(const QPointF& p) const
    {
        const int x = std::clamp(static_cast<int>((p.x() - origin_.x()) / cell_), 0, nx_ - 1);
        const int y = std::clamp(static_cast<int>((p.y() - origin_.y()) / cell_), 0, ny_ - 1);
        return static_cast<size_t>(y) * static_cast<size_t>(nx_) + static_cast<size_t>(x);
    }

    double cell_ = 1.0;
    QPointF origin_;
    int nx_ = 1, ny_ = 1;
    std::vector<std::vector<IndexEntry>> cells_;
};

QRectF boundsOf(const std::vector<Contour>& contours)
{
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const Contour& c : contours) {
        for (const QPointF& p : c.points) {
            x0 = std::min(x0, p.x());
            x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y());
            y1 = std::max(y1, p.y());
        }
    }
    if (x0 > x1) return QRectF(0, 0, 1, 1);
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

struct VectorPathContext {
    const GcodeJob* job;
    const std::vector<Contour>* contours;   // Umrisse in mm (Bedienersicht, Mitte als Ursprung)
    double maxPowerS;
    double overscan;
    int passes;
    double spacing;                          // Linienabstand bei Full
    double phi;                              // Richtung der Zeilen
    double nMin;                             // kleinster Querabstand des Auftrags
    std::function<QPointF(const QPointF&)> pointToMachine;
    std::function<QPointF(double, double)> lineToMachine;
    QPointF originPoint;                     // Lage des Job-Ursprungs (= Nullpunkt der Maschine)
};

// Cutting: alle Umrisse. Ein Umriss mit Löchern oder Inseln darin kommt erst dran, wenn alle darin
// liegenden geschnitten sind (innere zuerst). Sonst geht der Laser immer zum nächstgelegenen
// Punkt eines noch nicht geschnittenen Umrisses; geschlossene Umrisse beginnen dort, wo der Laser
// ankommt, offene am näheren Ende. So bleiben die Leerfahrten auch bei sehr vielen Details kurz.
bool emitCutting(const VectorPathContext& c, Emitter& emitter, const std::atomic_bool* cancel,
                 const std::function<void(double)>& progress, bool& cancelled)
{
    const std::vector<Contour>& contours = *c.contours;
    const size_t count = contours.size();
    std::vector<int> waiting(count, 0);   // noch nicht geschnittene Kinder
    for (const Contour& contour : contours) {
        if (contour.parent >= 0) ++waiting[static_cast<size_t>(contour.parent)];
    }
    std::vector<char> done(count, 0);
    PointIndex index(boundsOf(contours), count * 4);
    auto makeEligible = [&](size_t i) {
        const Contour& contour = contours[i];
        if (contour.closed) {
            for (size_t k = 0; k < contour.points.size(); ++k) {
                index.add(contour.points[k], static_cast<int>(i), static_cast<int>(k));
            }
        } else {
            index.add(contour.points.front(), static_cast<int>(i), 0);
            index.add(contour.points.back(), static_cast<int>(i), static_cast<int>(contour.points.size() - 1));
        }
    };
    for (size_t i = 0; i < count; ++i) {
        if (waiting[i] == 0) makeEligible(i);
    }

    // Reihenfolge festlegen (ohne zu schreiben): Liste von Streckenzügen.
    std::vector<Ring> order;
    std::vector<int> orderPower;   // Leistung je Streckenzug (-1 = die des Auftrags)
    order.reserve(count);
    QPointF cur = c.originPoint;
    size_t remaining = count;
    while (remaining > 0) {
        if (cancel && (order.size() % 256) == 0 && cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            return false;
        }
        IndexEntry e;
        if (!index.nearest(cur, [&](int id) { return !done[static_cast<size_t>(id)]; }, e)) {
            break;   // darf nicht vorkommen; sicherheitshalber aufhören statt endlos zu suchen
        }
        const size_t i = static_cast<size_t>(e.id);
        const Contour& contour = contours[i];
        const Ring& pts = contour.points;
        Ring ring;
        if (contour.closed) {
            ring.reserve(pts.size() + 1);
            for (size_t k = 0; k <= pts.size(); ++k) {
                ring.push_back(pts[(static_cast<size_t>(e.sub) + k) % pts.size()]);   // letzter Punkt schließt
            }
            cur = ring.front();
        } else {
            ring = pts;
            if (e.sub != 0) std::reverse(ring.begin(), ring.end());
            cur = ring.back();
        }
        order.push_back(std::move(ring));
        orderPower.push_back(contour.powerS);
        done[i] = 1;
        --remaining;
        if (contour.parent >= 0 && --waiting[static_cast<size_t>(contour.parent)] == 0) {
            makeEligible(static_cast<size_t>(contour.parent));
        }
    }
    // Falls die Suche vorzeitig endete (sollte nicht passieren): Rest unoptimiert anhängen.
    for (size_t i = 0; remaining > 0 && i < count; ++i) {
        if (!done[i]) {
            Ring ring = contours[i].points;
            if (contours[i].closed) ring.push_back(ring.front());
            order.push_back(std::move(ring));
            orderPower.push_back(contours[i].powerS);
            --remaining;
        }
    }

    bool ok = true;
    const int power = static_cast<int>(std::lround(c.maxPowerS));
    const double total = static_cast<double>(c.passes) * static_cast<double>(order.size());
    double passDone = 0.0;
    for (int pass = 0; pass < c.passes && ok; ++pass) {
        for (size_t r = 0; r < order.size(); ++r) {
            const Ring& ring = order[r];
            const int ringPower = orderPower[r] >= 0 ? orderPower[r] : power;
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                cancelled = true;
                return false;
            }
            if (progress && (static_cast<long long>(passDone) % 64) == 0) {
                progress(passDone / total);
            }
            passDone += 1.0;
            ok = ok && emitter.move(true, c.pointToMachine(ring.front()), 0);
            for (size_t i = 1; i < ring.size() && ok; ++i) {
                ok = emitter.move(false, c.pointToMachine(ring[i]), ringPower);
            }
        }
    }
    return ok;
}

// Full: Jede zusammenhängende Fläche (Umriss samt Löchern) für sich im Zickzack füllen. Die
// Reihenfolge der Flächen richtet sich nach dem Weg: Der Laser geht jeweils zur Fläche, deren
// Einstieg (oben oder unten, je nachdem was näher ist) am nächsten liegt; die Fläche wird dazu
// notfalls von hinten nach vorn gefüllt. Auch sehr dünne Flächen (schmaler als der Linienabstand)
// bekommen mindestens eine Zeile.
bool emitFull(const VectorPathContext& c, Emitter& emitter, const std::atomic_bool* cancel,
              const std::function<void(double)>& progress, bool& cancelled)
{
    const std::vector<Contour>& contours = *c.contours;
    const size_t count = contours.size();
    const double dx = std::cos(c.phi), dy = -std::sin(c.phi);
    const double nx = std::sin(c.phi), ny = std::cos(c.phi);
    const bool bidirectional = c.job->operation.bidirectional;

    // Flächen: jeder Umriss mit gerader Tiefe samt seinen Löchern (direkte Kinder).
    std::vector<std::vector<int>> shapes;
    {
        std::vector<int> shapeOf(count, -1);
        for (size_t i = 0; i < count; ++i) {
            if (contours[i].depth % 2 == 0) {
                shapeOf[i] = static_cast<int>(shapes.size());
                shapes.push_back({static_cast<int>(i)});
            }
        }
        for (size_t i = 0; i < count; ++i) {
            if (contours[i].depth % 2 == 1 && contours[i].parent >= 0 &&
                shapeOf[static_cast<size_t>(contours[i].parent)] >= 0) {
                shapes[static_cast<size_t>(shapeOf[static_cast<size_t>(contours[i].parent)])].push_back(static_cast<int>(i));
            }
        }
    }

    // Pinselstrich-Weg (siehe planung.md 3.18): Jede Fläche wird in Balken zerlegt. Ein Balken ist eine Kette von
    // Läufen (Stücken je Zeile), in der jeder Lauf genau einen Nachbarn in der nächsten Zeile hat. Wo sich eine
    // Kette verzweigt oder zusammenläuft (Rahmen: oben, links und rechts, unten), beginnt ein neuer Balken.
    // Jeder Balken wird im Zickzack gefüllt; danach geht der Laser ohne Abschalten in einen angrenzenden Balken.
    const int power = static_cast<int>(std::lround(c.maxPowerS));
    const double maxShift = 25.0 * c.spacing;
    std::vector<Strip> strips;
    std::vector<int> shapeFirstStrip(shapes.size() + 1, 0);
    {
        std::vector<double> hits;
        std::vector<StripRun> flat;
        std::vector<int> rowStart;
        std::vector<int> upCount, downCount, upIdx, stripOf;
        std::vector<std::pair<int, int>> edges;
        for (size_t s = 0; s < shapes.size(); ++s) {
            shapeFirstStrip[s] = static_cast<int>(strips.size());
            if (cancel && (s % 64) == 0 && cancel->load(std::memory_order_relaxed)) {
                cancelled = true;
                return false;
            }
            if (progress && (s % 64) == 0) {
                progress(0.4 * static_cast<double>(s) / static_cast<double>(shapes.size()));
            }
            const std::vector<int>& rings = shapes[s];
            double nLo = std::numeric_limits<double>::max(), nHi = std::numeric_limits<double>::lowest();
            for (const int r : rings) {
                for (const QPointF& p : contours[static_cast<size_t>(r)].points) {
                    const double n = p.x() * nx + p.y() * ny;
                    nLo = std::min(nLo, n);
                    nHi = std::max(nHi, n);
                }
            }
            int kFirst = static_cast<int>(std::ceil((nLo - c.nMin) / c.spacing - 0.5));
            int kLast = static_cast<int>(std::floor((nHi - c.nMin) / c.spacing - 0.5));
            // Sehr dünne Fläche: Keine Rasterzeile trifft sie. Dann genau eine Zeile durch die Mitte.
            const bool thin = kLast < kFirst;
            const double thinNk = (nLo + nHi) / 2.0;
            const int lineCount = thin ? 1 : kLast - kFirst + 1;
            std::vector<std::vector<double>> perLine(static_cast<size_t>(lineCount));
            auto lineNk = [&](int k) { return thin ? thinNk : c.nMin + (k + 0.5) * c.spacing; };
            for (const int r : rings) {
                const Ring& pts = contours[static_cast<size_t>(r)].points;
                for (size_t i = 0; i < pts.size(); ++i) {
                    const QPointF& a = pts[i];
                    const QPointF& b = pts[(i + 1) % pts.size()];
                    const double na = a.x() * nx + a.y() * ny;
                    const double nb = b.x() * nx + b.y() * ny;
                    if (na == nb) continue;
                    const double ta = a.x() * dx + a.y() * dy;
                    const double tb = b.x() * dx + b.y() * dy;
                    const double lo = std::min(na, nb), hi = std::max(na, nb);
                    int k0, k1;
                    if (thin) {
                        // Die eine Zeile gilt, wenn sie zwischen lo und hi liegt (halboffen).
                        if (!(lo <= thinNk && thinNk < hi)) continue;
                        k0 = k1 = 0;
                    } else {
                        // Zeilen k mit lo <= nk < hi (halboffen, damit Ecken nicht doppelt zählen).
                        k0 = std::max(kFirst, static_cast<int>(std::ceil((lo - c.nMin) / c.spacing - 0.5)));
                        k1 = std::min(kLast, static_cast<int>(std::ceil((hi - c.nMin) / c.spacing - 0.5)) - 1);
                    }
                    for (int k = k0; k <= k1; ++k) {
                        const double nk = thin ? thinNk : lineNk(k);
                        perLine[static_cast<size_t>(thin ? 0 : k - kFirst)].push_back(ta + (nk - na) / (nb - na) * (tb - ta));
                    }
                }
            }
            // Läufe je Zeile (paarweise Schnittpunkte).
            flat.clear();
            rowStart.assign(static_cast<size_t>(lineCount) + 1, 0);
            for (int k = 0; k < lineCount; ++k) {
                rowStart[static_cast<size_t>(k)] = static_cast<int>(flat.size());
                hits = std::move(perLine[static_cast<size_t>(k)]);
                if (hits.size() < 2) continue;
                std::sort(hits.begin(), hits.end());
                const double nk = thin ? thinNk : lineNk(kFirst + k);
                for (size_t i = 0; i + 1 < hits.size(); i += 2) {
                    if (hits[i + 1] - hits[i] > 1e-6) flat.push_back({hits[i], hits[i + 1], nk, k});
                }
            }
            rowStart[static_cast<size_t>(lineCount)] = static_cast<int>(flat.size());
            if (flat.empty()) continue;
            const int base = static_cast<int>(strips.size());
            const int shapePower = contours[static_cast<size_t>(rings.front())].powerS;
            if (flat.size() == 1) {   // der häufigste Fall (zum Beispiel ein Punkt): ein Balken mit einem Lauf
                strips.emplace_back();
                strips.back().runs.push_back(flat.front());
                strips.back().power = shapePower;
                continue;
            }
            // Läufe in benachbarten Zeilen, die sich überlappen, sind verbunden.
            const size_t n = flat.size();
            upCount.assign(n, 0);
            downCount.assign(n, 0);
            upIdx.assign(n, -1);
            edges.clear();
            for (int k = 0; k + 1 < lineCount; ++k) {
                int i = rowStart[static_cast<size_t>(k)];
                const int iEnd = rowStart[static_cast<size_t>(k) + 1];
                int j = iEnd;
                const int jEnd = rowStart[static_cast<size_t>(k) + 2];
                while (i < iEnd && j < jEnd) {
                    const StripRun& a = flat[static_cast<size_t>(i)];
                    const StripRun& b = flat[static_cast<size_t>(j)];
                    if (std::min(a.t1, b.t1) - std::max(a.t0, b.t0) > 1e-9) {
                        ++downCount[static_cast<size_t>(i)];
                        ++upCount[static_cast<size_t>(j)];
                        upIdx[static_cast<size_t>(j)] = i;
                        edges.emplace_back(i, j);
                    }
                    if (a.t1 < b.t1) ++i; else ++j;
                }
            }
            // Balken: Ein Lauf setzt den Balken seines einzigen oberen Nachbarn fort, wenn dieser ebenfalls
            // nur ihn als unteren Nachbarn hat. Sonst beginnt ein neuer Balken.
            stripOf.assign(n, -1);
            for (size_t i = 0; i < n; ++i) {
                const int up = upIdx[i];
                if (upCount[i] == 1 && up >= 0 && downCount[static_cast<size_t>(up)] == 1) {
                    stripOf[i] = stripOf[static_cast<size_t>(up)];
                } else {
                    stripOf[i] = static_cast<int>(strips.size());
                    strips.emplace_back();
                }
                strips[static_cast<size_t>(stripOf[i])].runs.push_back(flat[i]);
            }
            for (const auto& [a, b] : edges) {
                const int sa = stripOf[static_cast<size_t>(a)], sb = stripOf[static_cast<size_t>(b)];
                if (sa != sb) {
                    strips[static_cast<size_t>(sa)].bottomNb.push_back(sb);
                    strips[static_cast<size_t>(sb)].topNb.push_back(sa);
                }
            }
            for (size_t k = static_cast<size_t>(base); k < strips.size(); ++k) strips[k].power = shapePower;
        }
        shapeFirstStrip[shapes.size()] = static_cast<int>(strips.size());
    }

    // Start der ersten Zeile eines Balkens. end: 0 = oberes Ende (erste Zeile), 1 = unteres Ende (letzte Zeile).
    // side: 0 = die Zeile läuft vorwärts (beginnt bei t0), 1 = rückwärts (beginnt bei t1); mit Überscan.
    auto pointAt = [&](double t, double nk) { return QPointF(nx * nk + dx * t, ny * nk + dy * t); };
    auto entryPoint = [&](const Strip& st, int end, int side) {
        const StripRun& r = end == 0 ? st.runs.front() : st.runs.back();
        return side == 0 ? pointAt(r.t0 - c.overscan, r.nk) : pointAt(r.t1 + c.overscan, r.nk);
    };
    auto endOf = [&](const FillLine& l) { return l.forward ? pointAt(l.t1 + c.overscan, l.nk) : pointAt(l.t0 - c.overscan, l.nk); };
    const int sides = bidirectional ? 2 : 1;   // einseitig laufen alle Zeilen vorwärts

    // Die Zeilen eines Balkens im Zickzack (von oben nach unten oder umgekehrt). Gibt die Zeile am Ende zurück.
    std::vector<FillLine> all;
    auto emitStrip = [&](int si, int end, int side) {
        const Strip& st = strips[static_cast<size_t>(si)];
        const size_t n = st.runs.size();
        bool forward = side == 0;
        for (size_t k = 0; k < n; ++k) {
            const StripRun& r = end == 0 ? st.runs[k] : st.runs[n - 1 - k];
            FillLine line{r.t0, r.t1, r.nk, bidirectional ? forward : true, r.row, false, strips[static_cast<size_t>(si)].power};
            if (bidirectional) forward = !forward;
            if (k > 0 && bidirectional) {
                // Zickzack ohne Absetzen: Der Laser bleibt am Zeilenende an und fährt entlang des Randes in die nächste Zeile.
                FillLine& a = all.back();
                const double endA = a.forward ? a.t1 : a.t0;
                const double startB = line.forward ? line.t0 : line.t1;
                if (!(line.t0 > a.t1 || a.t0 > line.t1) && std::abs(endA - startB) <= maxShift) a.joinNext = true;
            }
            all.push_back(line);
        }
    };

    // Reihenfolge der Flächen: immer zur nächsten (Einstieg an einem Ende eines Balkens). Innerhalb einer Fläche
    // geht der Laser von Balken zu Balken, wo sich ein angrenzender Balken ohne Absetzen erreichen lässt.
    PointIndex index(boundsOf(contours), strips.size() * 2);
    for (size_t s = 0; s < shapes.size(); ++s) {
        for (int si = shapeFirstStrip[s]; si < shapeFirstStrip[s + 1]; ++si) {
            const Strip& st = strips[static_cast<size_t>(si)];
            for (int end = 0; end < (st.runs.size() == 1 ? 1 : 2); ++end) {
                for (int side = 0; side < sides; ++side) {
                    index.add(entryPoint(st, end, side), static_cast<int>(s), si * 4 + end * 2 + side);
                }
            }
        }
    }
    std::vector<char> done(shapes.size(), 0);
    {
        size_t total = 0;
        for (const Strip& st : strips) total += st.runs.size();
        all.reserve(total);
    }
    QPointF cur = c.originPoint;
    for (size_t n = 0;; ++n) {
        if (cancel && (n % 64) == 0 && cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            return false;
        }
        IndexEntry e;
        if (!index.nearest(cur, [&](int id) { return !done[static_cast<size_t>(id)]; }, e)) break;
        const size_t s = static_cast<size_t>(e.id);
        done[s] = 1;
        int si = e.sub / 4, end = (e.sub / 2) % 2, side = e.sub % 2;
        const int first = shapeFirstStrip[s], last = shapeFirstStrip[s + 1];
        int remaining = last - first;
        std::unique_ptr<PointIndex> local;   // nur bei großen Flächen (siehe unten)
        while (true) {
            Strip& st = strips[static_cast<size_t>(si)];
            st.done = true;
            --remaining;
            emitStrip(si, end, side);
            cur = endOf(all.back());
            if (remaining == 0) break;
            // Weiter in einen angrenzenden Balken am Ende, an dem der Laser jetzt steht.
            const int farEnd = 1 - end;
            const std::vector<int>& neighbors = farEnd == 1 ? st.bottomNb : st.topNb;
            const int nextEnd = farEnd == 1 ? 0 : 1;   // der Nachbar wird an dem Ende betreten, das an diesen Balken grenzt
            int bestStrip = -1, bestSide = 0;
            double bestShift = std::numeric_limits<double>::max();
            if (bidirectional) {
                const FillLine& lastLine = all.back();
                const double endT = lastLine.forward ? lastLine.t1 : lastLine.t0;
                for (const int nb : neighbors) {
                    const Strip& cand = strips[static_cast<size_t>(nb)];
                    if (cand.done) continue;
                    const StripRun& r = nextEnd == 0 ? cand.runs.front() : cand.runs.back();
                    for (int sd = 0; sd < 2; ++sd) {
                        const double shift = std::abs(endT - (sd == 0 ? r.t0 : r.t1));
                        if (shift <= maxShift && shift < bestShift) {
                            bestShift = shift;
                            bestStrip = nb;
                            bestSide = sd;
                        }
                    }
                }
            }
            if (bestStrip >= 0) {
                all.back().joinNext = true;   // der Laser bleibt an und fährt in den nächsten Balken
                si = bestStrip;
                end = nextEnd;
                side = bestSide;
                continue;
            }
            // Kein Balken ohne Absetzen erreichbar: zum nächsten noch nicht gefüllten Balken dieser Fläche.
            double bestDist = std::numeric_limits<double>::max();
            int pickStrip = -1, pickEnd = 0, pickSide = 0;
            auto consider = [&](int cs) {
                const Strip& cand = strips[static_cast<size_t>(cs)];
                for (int ce = 0; ce < (cand.runs.size() == 1 ? 1 : 2); ++ce) {
                    for (int sd = 0; sd < sides; ++sd) {
                        const QPointF p = entryPoint(cand, ce, sd);
                        const double d = sqr(p.x() - cur.x()) + sqr(p.y() - cur.y());
                        if (d < bestDist) {
                            bestDist = d;
                            pickStrip = cs;
                            pickEnd = ce;
                            pickSide = sd;
                        }
                    }
                }
            };
            for (const int nb : neighbors) {
                if (!strips[static_cast<size_t>(nb)].done) consider(nb);
            }
            if (pickStrip < 0) {
                if (last - first > 48) {
                    // Große Fläche (zum Beispiel ein dunkler Bereich im Dithering mit vielen tausend Balken):
                    // ein eigener räumlicher Index statt linearer Suche.
                    if (!local) {
                        double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = std::numeric_limits<double>::lowest(), y1 = x1;
                        std::vector<std::pair<QPointF, int>> pts;
                        for (int cs = first; cs < last; ++cs) {
                            const Strip& cand = strips[static_cast<size_t>(cs)];
                            for (int ce = 0; ce < (cand.runs.size() == 1 ? 1 : 2); ++ce) {
                                for (int sd = 0; sd < sides; ++sd) {
                                    const QPointF p = entryPoint(cand, ce, sd);
                                    x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
                                    y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
                                    pts.emplace_back(p, cs * 4 + ce * 2 + sd);
                                }
                            }
                        }
                        local = std::make_unique<PointIndex>(QRectF(QPointF(x0, y0), QPointF(x1, y1)), pts.size());
                        for (const auto& [p, code] : pts) local->add(p, code / 4, code % 4);
                    }
                    IndexEntry le;
                    if (local->nearest(cur, [&](int id) { return !strips[static_cast<size_t>(id)].done; }, le)) {
                        pickStrip = le.id;
                        pickEnd = le.sub / 2;
                        pickSide = le.sub % 2;
                    }
                } else {
                    for (int cs = first; cs < last; ++cs) {
                        if (!strips[static_cast<size_t>(cs)].done) consider(cs);
                    }
                }
            }
            if (pickStrip < 0) break;
            si = pickStrip;
            end = pickEnd;
            side = pickSide;
        }
    }

    bool ok = true;
    const double total = static_cast<double>(c.passes) * static_cast<double>(all.size());
    double passDone = 0.0;
    for (int pass = 0; pass < c.passes && ok; ++pass) {
        bool joined = false;
        for (const FillLine& line : all) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                cancelled = true;
                return false;
            }
            if (progress && (static_cast<long long>(passDone) % 256) == 0) {
                progress(0.4 + 0.6 * passDone / total);
            }
            passDone += 1.0;
            const int linePower = line.power >= 0 ? line.power : power;
            const double startT = line.forward ? line.t0 : line.t1;
            const double endT = line.forward ? line.t1 : line.t0;
            const double sign = line.forward ? 1.0 : -1.0;   // Laufrichtung entlang t
            if (joined) {
                // Der Laser ist noch an: der Schritt in diese Zeile wird mit Leistung gefahren.
                ok = ok && emitter.move(false, c.lineToMachine(startT, line.nk), linePower);
            } else {
                ok = ok && emitter.move(true, c.lineToMachine(startT - sign * c.overscan, line.nk), 0);
                if (c.overscan > 0.0) ok = ok && emitter.move(false, c.lineToMachine(startT, line.nk), 0);
            }
            ok = ok && emitter.move(false, c.lineToMachine(endT, line.nk), linePower);
            if (line.joinNext) {
                joined = true;
            } else {
                joined = false;
                if (c.overscan > 0.0) ok = ok && emitter.move(false, c.lineToMachine(endT + sign * c.overscan, line.nk), 0);
            }
        }
    }
    return ok;
}

}  // namespace

GcodeResult generateGcode(const GcodeJob& job, const GcodeSink& sink, const std::atomic_bool* cancel,
                          const std::function<void(double)>& progress)
{
    GcodeResult result;
    const ObjectTransform& tf = job.transform;
    const Operation& op = job.operation;
    const double pitch = job.pixelSizeMm;

    // ------------------------------------------------------------------ Prüfungen
    if (job.raster.isNull() || job.raster.format() != QImage::Format_Grayscale8) {
        result.error = tr("Es gibt noch kein berechnetes Bild.");
        return result;
    }
    if (tf.widthMm <= 0.0 || tf.heightMm <= 0.0 || pitch <= 0.0 || op.speedMmPerMin <= 0.0 ||
        job.machine.maxPower <= 0 || job.machine.rapidMmPerMin <= 0.0) {
        result.error = tr("Ungültige Einstellungen (Größe, Pixelgröße, Speed oder Maschinenwerte).");
        return result;
    }
    const QSizeF bounds = boundingSize(tf);
    if (!fitsWorkArea(bounds, job.machine)) {
        const QSizeF area = workAreaInOperatorView(job.machine);
        result.error = tr("Der Auftrag (%1 × %2 mm) ist größer als der Arbeitsbereich (%3 × %4 mm). "
                          "Bitte verkleinern.")
                           .arg(bounds.width(), 0, 'f', 1)
                           .arg(bounds.height(), 0, 'f', 1)
                           .arg(area.width(), 0, 'f', 0)
                           .arg(area.height(), 0, 'f', 0);
        return result;
    }

    // ------------------------------------------------------------------ Geometrie
    // Alle Rechnungen im Koordinatensystem des Bedieners mit dem Mittelpunkt des Auftrags als
    // Ursprung, X nach rechts, Y nach unten.
    const double theta = tf.rotationDeg * std::numbers::pi / 180.0;
    const double ct = std::cos(theta);
    const double st = std::sin(theta);
    const double phi = std::clamp(op.angleDeg, 0.0, 180.0) * std::numbers::pi / 180.0;
    const double dx = std::cos(phi), dy = -std::sin(phi);   // Laufrichtung der Zeilen
    const double nx = std::sin(phi), ny = std::cos(phi);    // Richtung, in der die Zeilen aufeinanderfolgen

    double nMin = std::numeric_limits<double>::max(), nMax = std::numeric_limits<double>::lowest();
    double tMin = nMin, tMax = nMax;
    for (const int su : {-1, 1}) {
        for (const int sv : {-1, 1}) {
            const double u = su * tf.widthMm / 2.0;
            const double v = sv * tf.heightMm / 2.0;
            const double x = u * ct - v * st;
            const double y = u * st + v * ct;
            nMin = std::min(nMin, x * nx + y * ny);
            nMax = std::max(nMax, x * nx + y * ny);
            tMin = std::min(tMin, x * dx + y * dy);
            tMax = std::max(tMax, x * dx + y * dy);
        }
    }
    const int lineCount = std::max(1, static_cast<int>(std::ceil((nMax - nMin) / pitch - 1e-6)));
    const int cellCount = std::max(1, static_cast<int>(std::ceil((tMax - tMin) / pitch - 1e-6)));

    // Lage des Job-Ursprungs im Auftrag.
    const QPointF fraction = jobOriginFraction(job.origin);
    // Bei einem Teilauftrag (placed) ist die Lage des Mittelpunkts gegenüber dem Ursprung vorgegeben.
    // Ein Versatz der Grafik zum Nullpunkt (job.transform.offset*) verschiebt den Ursprungspunkt entsprechend.
    const QPointF originPoint = job.placed ? QPointF(-job.centerFromOriginMm.x(), -job.centerFromOriginMm.y())
                                           : QPointF(-bounds.width() / 2.0 + fraction.x() * bounds.width() - job.transform.offsetXMm,
                                                     -bounds.height() / 2.0 + fraction.y() * bounds.height() - job.transform.offsetYMm);

    // Leistung aus dem Grauwert: weiß (255) = 0, schwarz (0) = eingestellte Leistung.
    const int maxPower = std::round(std::clamp(op.powerPercent, 0.0, 100.0) / 100.0 * job.machine.maxPower);
    int powerTable[256];
    const double basePower = std::clamp(op.powerPercent, 0.0, 100.0) / 100.0 * job.machine.maxPower;
    const GrayPowerSettings& gp = job.grayPower;
    const int levels = std::clamp(gp.levels, 2, 255);
    for (int g = 0; g < 256; ++g) {
        if (g == 255) {
            powerTable[g] = 0;   // reines Weiß bleibt immer aus
        } else if (job.grayPowerEnabled) {
            // Graustufen: von Mindest- bis Höchstleistung, auf Stufen gerundet.
            const double t = (255 - g) / 255.0;
            const int k = std::clamp(static_cast<int>(std::lround(t * levels)), 1, levels);
            const double percent = gp.minPercent + (gp.maxPercent - gp.minPercent) * static_cast<double>(k) / levels;
            powerTable[g] = std::max(percent > 0.0 ? 1 : 0, static_cast<int>(std::lround(basePower * percent / 100.0)));
        } else {
            powerTable[g] = static_cast<int>(std::lround(maxPower * (255 - g) / 255.0));
        }
    }

    auto toMachine = [&](double t, double nk) {
        const double px = nx * nk + dx * t - originPoint.x();
        const double py = ny * nk + dy * t - originPoint.y();
        return operatorToMachineCorrected(job.machine, QPointF(px, -py));   // Y nach oben
    };
    auto cellStart = [&](int i) { return tMin + i * pitch; };

    // ------------------------------------------------------------------ Kopf
    Writer writer(sink);
    GcodeStats& stats = result.stats;
    bool ok = true;
    if (writer.enabled() && !job.omitHeader) {
        ok = ok && writer.line("(Laser-Dilettant G-Code)");
        if (!job.note.isEmpty()) {
            ok = ok && writer.line("(" + asciiComment(job.note) + ")");
        }
        if (!job.title.isEmpty()) {
            ok = ok && writer.line("(Bild: " + asciiComment(job.title) + ")");
        }
        ok = ok && writer.line("(Groesse: " + number1(tf.widthMm) + " x " + number1(tf.heightMm) +
                               " mm, Pixelgroesse: " + QByteArray::number(pitch, 'g', 4) + " mm)");
        ok = ok && writer.line("(Speed: " + formatGcodeNumber(op.speedMmPerMin) + " mm/min, Leistung: " +
                               number1(op.powerPercent) + " %, Durchgaenge: " + QByteArray::number(op.passes) +
                               ", Ueberscan: " + formatGcodeNumber(op.overscanMm) + " mm)");
        if (op.mode == EngraveMode::Cutting) {
            ok = ok && writer.line("(Art: Cutting, Kurvengenauigkeit: " + formatGcodeNumber(op.toleranceMm) + " mm)");
        } else {
            ok = ok && writer.line("(Art: " + QByteArray(op.mode == EngraveMode::Full ? "Full" : "Linie") +
                                   ", Richtung: " + number1(op.angleDeg) + " Grad, " +
                                   (op.bidirectional ? "beidseitig" : "einseitig") + ")");
            if (op.mode == EngraveMode::Full) {
                ok = ok && writer.line("(Linienabstand: " + formatGcodeNumber(pitch) + " mm)");
            }
        }
    }
    const auto codeLines = [](const QString& code) {
        QList<QByteArray> lines;
        for (const QString& raw : code.split(QLatin1Char('\n'))) {
            const QByteArray line = raw.trimmed().toLatin1();
            if (!line.isEmpty()) {
                lines.append(line);
            }
        }
        return lines;
    };
    if (!job.omitHeader) {
        bool hasG54 = false;
        for (const QByteArray& line : codeLines(job.machine.startCode)) {
            ok = ok && writer.line(line);
            hasG54 = hasG54 || line.toUpper().contains("G54");
        }
        if (!hasG54) {
            ok = ok && writer.line("G54");   // sicher das Arbeitskoordinatensystem, in dem "Hier ist Null" gesetzt wird
        }
        ok = ok && writer.line(job.machine.laserCommand == LaserCommand::M4 ? "M4 S0" : "M3 S0");
        if (job.machine.moveToZeroBeforeStart) {
            ok = ok && writer.line("G0 X0 Y0");   // Profil: vor dem Start zum Nullpunkt fahren (Laser ist aus)
        }
    } else {
        ok = ok && writer.line("S0");   // ein Teilauftrag beginnt mit ausgeschaltetem Laser (S vom vorigen Teil zurücksetzen)
    }

    // ------------------------------------------------------------------ Zeilen
    Emitter emitter(writer, stats, op.speedMmPerMin, job.machine);
    const double halfW = tf.widthMm / 2.0;
    const double halfH = tf.heightMm / 2.0;
    const int rasterW = job.raster.width();
    const int rasterH = job.raster.height();
    const int passes = std::max(1, op.passes);
    const double overscan = std::max(0.0, op.overscanMm);
    std::vector<Run> runs;
    long long emittedLines = 0;

    if (op.mode != EngraveMode::Line) {
        // ---------------------------------------------------------------- Full und Cutting
        std::vector<Contour> contours;
        if (op.mode == EngraveMode::Cutting && job.svg) {
            // SVG: Striche und Füllungsumrisse direkt aus den Vektordaten (im Raum der Rasterpixel).
            contours = svgCutContours(*job.svg, job.svgSettings, std::max(0.001, op.toleranceMm), tf.widthMm,
                                      QSizeF(rasterW, rasterH));
        } else if (job.pixelAreas) {
            // Dithering und Graustufen: Jede Fläche gleicher Leistung (auch ein einzelner Punkt) ist eine Fläche
            // mit pixelgenauem Umriss (siehe planung.md 3.18).
            std::array<int, 256> powerOfGray{};
            for (int g = 0; g < 256; ++g) powerOfGray[static_cast<size_t>(g)] = powerTable[g];
            VectorizeResult vec = vectorizePixelAreas(job.raster, powerOfGray, cancel);
            if (vec.cancelled) {
                result.cancelled = true;
                return result;
            }
            if (!vec.ok) {
                result.error = vec.error;
                return result;
            }
            contours = std::move(vec.contours);
        } else {
            const VectorizeResult vec = vectorizeRaster(job.raster, job.vectorize, std::max(0.001, op.toleranceMm),
                                                        tf.widthMm / rasterW, cancel);
            if (vec.cancelled) {
                result.cancelled = true;
                return result;
            }
            if (!vec.ok) {
                result.error = vec.error;
                return result;
            }
            contours = vec.contours;
        }
        // Umrisse von Rasterpixeln in mm (Bedienersicht, Mitte des Auftrags als Ursprung).
        for (Contour& contour : contours) {
            for (QPointF& p : contour.points) {
                double u = p.x() / rasterW * tf.widthMm - halfW;
                double v = p.y() / rasterH * tf.heightMm - halfH;
                if (tf.flipHorizontal) u = -u;
                if (tf.flipVertical) v = -v;
                p = QPointF(u * ct - v * st, u * st + v * ct);
            }
        }
        VectorPathContext ctx;
        ctx.job = &job;
        ctx.contours = &contours;
        ctx.maxPowerS = maxPower;
        ctx.overscan = overscan;
        ctx.passes = passes;
        ctx.spacing = pitch;   // die Füllzeilen liegen im Abstand der Pixelgröße
        ctx.phi = phi;
        ctx.nMin = nMin;
        ctx.originPoint = originPoint;
        ctx.pointToMachine = [&](const QPointF& p) {
            return operatorToMachineCorrected(job.machine, QPointF(p.x() - originPoint.x(), -(p.y() - originPoint.y())));
        };
        ctx.lineToMachine = toMachine;
        bool cancelled = false;
        if (op.mode == EngraveMode::Cutting) {
            ctx.overscan = 0.0;
            ok = emitCutting(ctx, emitter, cancel, progress, cancelled);
        } else {
            ok = emitFull(ctx, emitter, cancel, progress, cancelled);
        }
        if (cancelled) {
            result.cancelled = true;
            return result;
        }
    } else {
        // Der Auftrag beginnt dort, wo der Job-Ursprung liegt: Liegt der Ursprung auf der Seite der hohen Zeilen
        // (n), laufen die Zeilen rückwärts durch (die erste Zeile ist die dem Ursprung nächste); liegt er auf der Seite
        // des Zeilenendes (t), beginnt die erste Zeile dort. Bei der Mitte bleibt alles wie es war.
        const double originN = originPoint.x() * nx + originPoint.y() * ny;
        const double originT = originPoint.x() * dx + originPoint.y() * dy;
        const bool reverseLines = originN > (nMin + nMax) / 2.0 + 1e-9;
        const bool startForward = originT <= (tMin + tMax) / 2.0 + 1e-9;
        for (int pass = 0; pass < passes && ok; ++pass) {
            for (int step = 0; step < lineCount && ok; ++step) {
                const int k = reverseLines ? lineCount - 1 - step : step;
                if (cancel && cancel->load(std::memory_order_relaxed)) {
                    result.cancelled = true;
                    return result;
                }
                if (progress && (step % 32) == 0) {
                    progress(static_cast<double>(pass * lineCount + step) / (static_cast<double>(passes) * lineCount));
                }

                const double nk = nMin + (k + 0.5) * pitch;
                // Die Gerade p(t) = n*nk + d*t im Koordinatensystem des Bildes: q(t) = A + B*t.
                const double aU = nk * (nx * ct + ny * st), bU = dx * ct + dy * st;
                const double aV = nk * (-nx * st + ny * ct), bV = -dx * st + dy * ct;
                double lo = -std::numeric_limits<double>::infinity();
                double hi = std::numeric_limits<double>::infinity();
                if (!clipSlab(aU, bU, halfW, lo, hi) || !clipSlab(aV, bV, halfH, lo, hi)) {
                    continue;
                }
                const int firstCell = std::max(0, static_cast<int>(std::ceil((lo - tMin) / pitch - 0.5)));
                const int lastCell = std::min(cellCount - 1, static_cast<int>(std::floor((hi - tMin) / pitch - 0.5)));

                // Zellen abtasten und gleiche Leistung zu Abschnitten zusammenfassen.
                runs.clear();
                for (int i = firstCell; i <= lastCell; ++i) {
                    const double t = tMin + (i + 0.5) * pitch;
                    double u = aU + bU * t;
                    double v = aV + bV * t;
                    if (tf.flipHorizontal) u = -u;
                    if (tf.flipVertical) v = -v;
                    const int ix = std::clamp(static_cast<int>(std::floor((u + halfW) / tf.widthMm * rasterW)), 0, rasterW - 1);
                    const int iy = std::clamp(static_cast<int>(std::floor((v + halfH) / tf.heightMm * rasterH)), 0, rasterH - 1);
                    const int power = powerTable[job.raster.constScanLine(iy)[ix]];
                    if (!runs.empty() && runs.back().power == power) {
                        runs.back().last = i + 1;
                    } else {
                        runs.push_back({i, i + 1, power});
                    }
                }
                // Leere Zeilenenden abschneiden; ganz leere Zeilen überspringen.
                while (!runs.empty() && runs.back().power == 0) runs.pop_back();
                if (runs.empty()) {
                    continue;
                }
                const size_t firstBurn = static_cast<size_t>(
                    std::find_if(runs.begin(), runs.end(), [](const Run& r) { return r.power > 0; }) - runs.begin());
                runs.erase(runs.begin(), runs.begin() + static_cast<std::ptrdiff_t>(firstBurn));

                const double tStart = cellStart(runs.front().first);
                const double tEnd = cellStart(runs.back().last);
                const bool forward = op.bidirectional ? ((emittedLines % 2 == 0) == startForward) : startForward;
                ++emittedLines;

                if (forward) {
                    ok = ok && emitter.move(true, toMachine(tStart - overscan, nk), 0);
                    if (overscan > 0.0) ok = ok && emitter.move(false, toMachine(tStart, nk), 0);
                    for (const Run& run : runs) ok = ok && emitter.move(false, toMachine(cellStart(run.last), nk), run.power);
                    if (overscan > 0.0) ok = ok && emitter.move(false, toMachine(tEnd + overscan, nk), 0);
                } else {
                    ok = ok && emitter.move(true, toMachine(tEnd + overscan, nk), 0);
                    if (overscan > 0.0) ok = ok && emitter.move(false, toMachine(tEnd, nk), 0);
                    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                        ok = ok && emitter.move(false, toMachine(cellStart(it->first), nk), it->power);
                    }
                    if (overscan > 0.0) ok = ok && emitter.move(false, toMachine(tStart - overscan, nk), 0);
                }
            }
        }

    }

    // ------------------------------------------------------------------ Ende
    if (!job.omitFooter) {
        bool hasLaserOff = false;
        const QList<QByteArray> endLines = codeLines(job.machine.endCode);
        for (const QByteArray& line : endLines) {
            ok = ok && writer.line(line);
            hasLaserOff = hasLaserOff || line.toUpper().contains("M5");
        }
        if (!hasLaserOff) {
            ok = ok && writer.line("M5");   // Der Laser wird am Ende immer ausgeschaltet.
        }
        if (job.machine.returnToZeroAfterJob) {
            ok = ok && writer.line("G0 X0 Y0");   // Profil: nach dem Auftrag zurück zum Nullpunkt
        }
    }
    // Fahrten im Ende-Code und die Rückfahrt zum Nullpunkt zählen zur Dauer.
    if (!job.omitFooter) {
        QByteArray endText = job.machine.endCode.toLatin1();
        if (job.machine.returnToZeroAfterJob) endText += "\nG0 X0 Y0";
        QBuffer buffer(&endText);
        buffer.open(QIODevice::ReadOnly);
        GcodeReader reader(&buffer);
        reader.setPosition(emitter.position().x(), emitter.position().y());
        GcodeMove move;
        while (reader.next(move)) {
            emitter.account(move);
        }
    }
    ok = ok && writer.flush();
    if (!ok) {
        result.error = tr("Der G-Code konnte nicht geschrieben werden.");
        return result;
    }
    if (progress) {
        progress(1.0);
    }
    stats.seconds = emitter.seconds();
    stats.lines = writer.lines();
    stats.bytes = writer.bytes();
    result.ok = true;
    return result;
}

namespace {

// Der Text des Rahmens aus den vier Ecken (in Maschinenkoordinaten): zur nächsten Ecke am Nullpunkt (entfällt, wenn der
// Kopf dort schon steht), einmal herum, Laser aus, zurück zum Nullpunkt.
QByteArray frameText(const MachineSetup& machine, const QPointF (&corners)[4], double speedMmPerMin, double powerPercent)
{
    const int power = static_cast<int>(std::lround(std::clamp(powerPercent, 0.0, 100.0) / 100.0 * machine.maxPower));
    QByteArray text = "(Laser-Dilettant Rahmen)\n";
    bool hasG54 = false;
    for (const QString& raw : machine.startCode.split(QLatin1Char('\n'))) {
        const QByteArray line = raw.trimmed().toLatin1();
        if (line.isEmpty()) continue;
        text += line + "\n";
        hasG54 = hasG54 || line.toUpper().contains("G54");
    }
    if (!hasG54) text += "G54\n";
    text += machine.laserCommand == LaserCommand::M4 ? "M4 S0\n" : "M3 S0\n";
    // Der Rahmen beginnt an der Ecke, die dem Nullpunkt (Job-Ursprung) am nächsten ist; die Umlaufrichtung bleibt.
    // Liegt der Kopf schon auf dieser Ecke (Ursprung = Ecke, keine Verschiebung), gibt es keine Leerfahrt.
    int start = 0;
    for (int i = 1; i < 4; ++i) {
        if (std::hypot(corners[i].x(), corners[i].y()) < std::hypot(corners[start].x(), corners[start].y()) - 1e-9) start = i;
    }
    const QPointF first = corners[start];
    if (std::hypot(first.x(), first.y()) > 0.0005) {
        text += "G0 X" + formatGcodeNumber(first.x()) + " Y" + formatGcodeNumber(first.y()) + "\n";
    }
    for (int i = 1; i <= 4; ++i) {
        const QPointF& c = corners[(start + i) % 4];
        text += "G1 X" + formatGcodeNumber(c.x()) + " Y" + formatGcodeNumber(c.y()) + " S" + QByteArray::number(power) +
                (i == 1 ? " F" + formatGcodeNumber(speedMmPerMin) : QByteArray()) + "\n";
    }
    text += "M5\nG0 X0 Y0\n";   // Laser aus, zurück zum Nullpunkt
    return text;
}

}  // namespace

FrameResult generateFrameGcode(const GcodeJob& job, double speedMmPerMin, double powerPercent)
{
    FrameResult result;
    const ObjectTransform& tf = job.transform;
    if (tf.widthMm <= 0.0 || tf.heightMm <= 0.0 || speedMmPerMin <= 0.0 || job.machine.maxPower <= 0) {
        result.error = tr("Ungültige Einstellungen (Größe, Speed oder Maschinenwerte).");
        return result;
    }
    const QSizeF bounds = boundingSize(tf);
    if (!fitsWorkArea(bounds, job.machine)) {
        const QSizeF area = workAreaInOperatorView(job.machine);
        result.error = tr("Der Auftrag (%1 × %2 mm) ist größer als der Arbeitsbereich (%3 × %4 mm). "
                          "Bitte verkleinern.")
                           .arg(bounds.width(), 0, 'f', 1)
                           .arg(bounds.height(), 0, 'f', 1)
                           .arg(area.width(), 0, 'f', 0)
                           .arg(area.height(), 0, 'f', 0);
        return result;
    }
    const QPointF fraction = jobOriginFraction(job.origin);
    const QPointF originPoint(-bounds.width() / 2.0 + fraction.x() * bounds.width() - tf.offsetXMm,
                              -bounds.height() / 2.0 + fraction.y() * bounds.height() - tf.offsetYMm);
    auto toMachine = [&](double x, double y) {
        return operatorToMachineCorrected(job.machine, QPointF(x - originPoint.x(), -(y - originPoint.y())));
    };
    // Der Rahmen folgt der blauen Linie der Grafik: die vier Ecken des ungedrehten Bildes (Breite x Höhe) werden um die
    // Mitte gedreht (Uhrzeigersinn, wie auf dem Bildschirm). Bei 0 Grad ist das dasselbe wie das umschließende Rechteck.
    // Das umschließende Rechteck (bounds) bestimmt weiter nur die Lage des Job-Ursprungs und die Größenprüfung.
    QTransform rotation;
    rotation.rotate(tf.rotationDeg);
    const double hw = tf.widthMm / 2.0, hh = tf.heightMm / 2.0;
    auto corner = [&](double dx, double dy) {
        const QPointF p = rotation.map(QPointF(dx, dy));
        return toMachine(p.x(), p.y());
    };
    const QPointF corners[] = {corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh)};
    result.gcode = frameText(job.machine, corners, speedMmPerMin, powerPercent);
    result.ok = true;
    return result;
}

FrameResult generateFrameGcodeForRect(const MachineSetup& machine, const QRectF& sceneRect, double speedMmPerMin,
                                      double powerPercent)
{
    FrameResult result;
    if (sceneRect.width() < 0.0 || sceneRect.height() < 0.0 || speedMmPerMin <= 0.0 || machine.maxPower <= 0) {
        result.error = tr("Ungültige Einstellungen (Größe, Speed oder Maschinenwerte).");
        return result;
    }
    if (!fitsWorkArea(sceneRect.size(), machine)) {
        const QSizeF area = workAreaInOperatorView(machine);
        result.error = tr("Der Auftrag (%1 × %2 mm) ist größer als der Arbeitsbereich (%3 × %4 mm).")
                           .arg(sceneRect.width(), 0, 'f', 1)
                           .arg(sceneRect.height(), 0, 'f', 1)
                           .arg(area.width(), 0, 'f', 0)
                           .arg(area.height(), 0, 'f', 0);
        return result;
    }
    auto toMachine = [&](double x, double y) { return operatorToMachineCorrected(machine, QPointF(x, -y)); };   // Szene: y nach unten
    const QPointF corners[] = {toMachine(sceneRect.left(), sceneRect.top()), toMachine(sceneRect.right(), sceneRect.top()),
                               toMachine(sceneRect.right(), sceneRect.bottom()), toMachine(sceneRect.left(), sceneRect.bottom())};
    result.gcode = frameText(machine, corners, speedMmPerMin, powerPercent);
    result.ok = true;
    return result;
}

}  // namespace ld::core
