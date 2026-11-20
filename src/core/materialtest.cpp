#include "core/materialtest.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace ld::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

// ---------------------------------------------------------------------------------------------
// Strichschrift: jede Ziffer ist ein Streckenzug in einem Raster von 2 Einheiten Breite und 4 Einheiten Höhe.
// ---------------------------------------------------------------------------------------------
struct Glyph {
    QChar ch;
    std::vector<std::vector<QPointF>> strokes;
};

const std::vector<Glyph>& glyphs()
{
    static const std::vector<Glyph> table = {
        {QLatin1Char('0'), {{{0, 0}, {2, 0}, {2, 4}, {0, 4}, {0, 0}}}},
        {QLatin1Char('1'), {{{0, 1}, {1, 0}, {1, 4}}}},
        {QLatin1Char('2'), {{{0, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 4}, {2, 4}}}},
        {QLatin1Char('3'), {{{0, 0}, {2, 0}, {2, 4}, {0, 4}}, {{0, 2}, {2, 2}}}},
        {QLatin1Char('4'), {{{0, 0}, {0, 2}, {2, 2}}, {{2, 0}, {2, 4}}}},
        {QLatin1Char('5'), {{{2, 0}, {0, 0}, {0, 2}, {2, 2}, {2, 4}, {0, 4}}}},
        {QLatin1Char('6'), {{{2, 0}, {0, 0}, {0, 4}, {2, 4}, {2, 2}, {0, 2}}}},
        {QLatin1Char('7'), {{{0, 0}, {2, 0}, {2, 4}}}},
        {QLatin1Char('8'), {{{0, 0}, {2, 0}, {2, 4}, {0, 4}, {0, 0}}, {{0, 2}, {2, 2}}}},
        {QLatin1Char('9'), {{{2, 2}, {0, 2}, {0, 0}, {2, 0}, {2, 4}, {0, 4}}}},
        {QLatin1Char(','), {{{1, 3.6}, {0.6, 4.8}}}},
        {QLatin1Char('.'), {{{1, 4}, {1, 4}}}},
        {QLatin1Char('-'), {{{0, 2}, {2, 2}}}},
        {QLatin1Char('X'), {{{0, 0}, {2, 4}}, {{2, 0}, {0, 4}}}},
        {QLatin1Char('Y'), {{{0, 0}, {1, 2}, {2, 0}}, {{1, 2}, {1, 4}}}},
    };
    return table;
}

constexpr double kGlyphAdvance = 3.0;   // Breite einer Ziffer plus Lücke, in Einheiten
constexpr double kGlyphHeight = 4.0;
constexpr double kGlyphDescent = 1.0;   // Platz unter der Grundlinie (Komma)

double strokeWidthMm(double heightMm) { return std::max(0.18, heightMm / 14.0); }

}  // namespace

double textWidthMm(const QString& text, double heightMm, double pixelMm)
{
    Q_UNUSED(pixelMm);
    if (text.isEmpty()) return 0.0;
    const double unit = heightMm / kGlyphHeight;
    return (text.size() * kGlyphAdvance - 1.0) * unit + strokeWidthMm(heightMm);
}

std::vector<std::vector<QPointF>> textStrokes(const QString& text, double heightMm)
{
    std::vector<std::vector<QPointF>> result;
    if (text.isEmpty() || heightMm <= 0.0) return result;
    const double unit = heightMm / kGlyphHeight;
    for (int i = 0; i < text.size(); ++i) {
        for (const Glyph& glyph : glyphs()) {
            if (glyph.ch != text[i]) continue;
            for (const auto& stroke : glyph.strokes) {
                std::vector<QPointF> line;
                for (const QPointF& p : stroke) {
                    line.push_back(QPointF((i * kGlyphAdvance + p.x()) * unit, p.y() * unit));
                }
                if (line.size() == 2 && line[0] == line[1]) {   // ein Punkt: kleine Strecke
                    line[1] += QPointF(unit * 0.2, 0.0);
                }
                result.push_back(std::move(line));
            }
        }
    }
    return result;
}

QImage textRaster(const QString& text, double heightMm, double pixelMm)
{
    if (text.isEmpty() || heightMm <= 0.0 || pixelMm <= 0.0) return {};
    const double unit = heightMm / kGlyphHeight;
    const double pen = strokeWidthMm(heightMm);
    const int w = std::max(1, static_cast<int>(std::ceil((textWidthMm(text, heightMm, pixelMm)) / pixelMm)));
    const int h = std::max(1, static_cast<int>(std::ceil(((kGlyphHeight + kGlyphDescent) * unit + pen) / pixelMm)));
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(255);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen linePen(Qt::black, pen / pixelMm, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(linePen);
    for (int i = 0; i < text.size(); ++i) {
        for (const Glyph& glyph : glyphs()) {
            if (glyph.ch != text[i]) continue;
            for (const auto& stroke : glyph.strokes) {
                QPolygonF poly;
                for (const QPointF& p : stroke) {
                    poly << QPointF((i * kGlyphAdvance + p.x()) * unit / pixelMm + pen / pixelMm / 2.0,
                                    p.y() * unit / pixelMm + pen / pixelMm / 2.0);
                }
                if (poly.size() == 2 && poly[0] == poly[1]) {   // ein Punkt
                    painter.drawPoint(poly[0]);
                } else {
                    painter.drawPolyline(poly);
                }
            }
        }
    }
    painter.end();
    return image;
}

// ---------------------------------------------------------------------------------------------
// Motive
// ---------------------------------------------------------------------------------------------
QImage motifRaster(TestMotif motif, double sizeMm, double pixelMm)
{
    const int n = std::max(2, static_cast<int>(std::lround(sizeMm / pixelMm)));
    QImage image(n, n, QImage::Format_Grayscale8);
    image.fill(255);
    const double outline = std::max(2.0, std::round(0.12 / pixelMm));   // Umrisslinien sind etwa einen Laserpunkt breit
    const double c = n / 2.0;
    // Linien des Verlaufs: Abstand von 2 mm (links) bis 0,5 mm (rechts), 0,25 mm breit, unter 45°.
    std::vector<double> lineCenters;   // Lage entlang der Diagonale (x + y) / sqrt(2), in Pixeln
    const double lineHalf = std::max(1.0, 0.25 / pixelMm / 2.0);
    if (motif == TestMotif::SquareGradient) {
        const double length = n * std::sqrt(2.0);   // Länge der Diagonale
        const double s0 = 2.0 / pixelMm, s1 = 0.5 / pixelMm;
        for (double pos = 0.0; pos <= length; ) {
            lineCenters.push_back(pos);
            pos += s0 + (s1 - s0) * (pos / length);
        }
    }
    for (int y = 0; y < n; ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < n; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            bool black = false;
            switch (motif) {
            case TestMotif::SquareFilled:
                black = true;
                break;
            case TestMotif::SquareOutline:
                black = x < outline || y < outline || x >= n - outline || y >= n - outline;
                break;
            case TestMotif::CircleFilled:
                black = std::hypot(px - c, py - c) <= c;
                break;
            case TestMotif::CircleOutline: {
                const double d = std::hypot(px - c, py - c);
                black = d <= c && d > c - outline;
                break;
            }
            case TestMotif::SquareGradient: {
                const double a = (px + py) / std::sqrt(2.0);
                const auto it = std::lower_bound(lineCenters.begin(), lineCenters.end(), a);
                if (it != lineCenters.end() && std::abs(*it - a) <= lineHalf) black = true;
                if (it != lineCenters.begin() && std::abs(*(it - 1) - a) <= lineHalf) black = true;
                break;
            }
            }
            row[x] = black ? 0 : 255;
        }
    }
    return image;
}

// ---------------------------------------------------------------------------------------------
// Aufbau
// ---------------------------------------------------------------------------------------------
QRectF MaterialTestLayout::cell(int column, int row, double sizeMm) const
{
    return QRectF(tableX + column * (sizeMm + gapMm), tableY + row * (sizeMm + gapMm), sizeMm, sizeMm);
}

MaterialTestLayout materialTestLayout(const MaterialTestSpec& spec)
{
    MaterialTestLayout layout;
    layout.gapMm = std::max(2.0, 0.4 * spec.sizeMm);
    const double margin = 2.0;   // Abstand zwischen Beschriftung und Tabelle
    double leftWidth = 0.0;
    for (const QString& label : spec.speedLabels) {   // links stehen die Speed-Werte (eine je Zeile)
        leftWidth = std::max(leftWidth, textWidthMm(label, spec.labelHeightMm, spec.pixelSizeMm));
    }
    const double topHeight = spec.labelHeightMm + margin;
    layout.tableX = leftWidth + (leftWidth > 0.0 ? margin : 0.0);
    layout.tableY = topHeight;
    const double tableSize = spec.cells * spec.sizeMm + (spec.cells - 1) * layout.gapMm;
    layout.total = QSizeF(layout.tableX + tableSize, layout.tableY + tableSize);
    for (int i = 0; i < spec.cells; ++i) {
        const QRectF c = layout.cell(i, 0, spec.sizeMm);
        const double w = i < static_cast<int>(spec.powerLabels.size())
                             ? textWidthMm(spec.powerLabels[static_cast<size_t>(i)], spec.labelHeightMm, spec.pixelSizeMm)
                             : 0.0;
        // Die Zahl steht zentriert über der Spalte (ragt sie über, ist das in Ordnung, solange sie in der Zeichnung bleibt).
        double x = c.center().x() - w / 2.0;
        x = std::max(0.0, std::min(x, layout.total.width() - w));
        layout.powerLabels.push_back(QRectF(x, 0.0, w, spec.labelHeightMm));
    }
    for (int j = 0; j < spec.cells; ++j) {
        const QRectF c = layout.cell(0, j, spec.sizeMm);
        const double w = j < static_cast<int>(spec.speedLabels.size())
                             ? textWidthMm(spec.speedLabels[static_cast<size_t>(j)], spec.labelHeightMm, spec.pixelSizeMm)
                             : 0.0;
        // rechtsbündig an der Tabelle, senkrecht zentriert zur Zeile
        layout.speedLabels.push_back(QRectF(layout.tableX - margin - w, c.center().y() - spec.labelHeightMm / 2.0, w,
                                            spec.labelHeightMm));
    }
    return layout;
}

QString validateMaterialTest(const MaterialTestSpec& spec)
{
    if (spec.cells < 1 || spec.cells > 9) return tr("Die Tabelle hat eine ungültige Größe.");
    if (static_cast<int>(spec.speeds.size()) != spec.cells || static_cast<int>(spec.powers.size()) != spec.cells) {
        return tr("Für jede Spalte (Power) und jede Zeile (Speed) wird ein Wert gebraucht.");
    }
    for (const double speed : spec.speeds) {
        if (speed <= 0.0) return tr("Jede Speed muss größer als 0 sein.");
    }
    for (const double power : spec.powers) {
        if (power < 0.0 || power > 100.0) return tr("Die Power liegt zwischen 0 und 100 %.");
    }
    if (spec.sizeMm <= 0.0 || spec.pixelSizeMm <= 0.0) return tr("Ungültige Größe oder Pixelgröße.");
    return {};
}

// ---------------------------------------------------------------------------------------------
// Vorschau
// ---------------------------------------------------------------------------------------------
QImage materialTestPreview(const MaterialTestSpec& spec, double pxPerMm)
{
    const MaterialTestLayout layout = materialTestLayout(spec);
    const int w = std::max(1, static_cast<int>(std::ceil(layout.total.width() * pxPerMm)));
    const int h = std::max(1, static_cast<int>(std::ceil(layout.total.height() * pxPerMm)));
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(255);
    QPainter painter(&image);
    auto place = [&](const QImage& raster, const QRectF& rect) {
        if (raster.isNull()) return;
        painter.drawImage(QRectF(rect.x() * pxPerMm, rect.y() * pxPerMm, rect.width() * pxPerMm, rect.height() * pxPerMm),
                          raster);
    };
    const QImage motif = motifRaster(spec.motif, spec.sizeMm, spec.pixelSizeMm);
    for (int j = 0; j < spec.cells; ++j) {
        for (int i = 0; i < spec.cells; ++i) place(motif, layout.cell(i, j, spec.sizeMm));
    }
    for (int i = 0; i < spec.cells && i < static_cast<int>(spec.powerLabels.size()); ++i) {
        const QRectF& r = layout.powerLabels[static_cast<size_t>(i)];
        const QImage text = textRaster(spec.powerLabels[static_cast<size_t>(i)], spec.labelHeightMm, spec.pixelSizeMm);
        place(text, QRectF(r.x(), r.y(), text.width() * spec.pixelSizeMm, text.height() * spec.pixelSizeMm));
    }
    for (int j = 0; j < spec.cells && j < static_cast<int>(spec.speedLabels.size()); ++j) {
        const QRectF& r = layout.speedLabels[static_cast<size_t>(j)];
        const QImage text = textRaster(spec.speedLabels[static_cast<size_t>(j)], spec.labelHeightMm, spec.pixelSizeMm);
        place(text, QRectF(r.x(), r.y(), text.width() * spec.pixelSizeMm, text.height() * spec.pixelSizeMm));
    }
    return image;
}

// ---------------------------------------------------------------------------------------------
// G-Code
// ---------------------------------------------------------------------------------------------
GcodeJob materialTestFrameJob(const MaterialTestSpec& spec, const MachineSetup& machine)
{
    const MaterialTestLayout layout = materialTestLayout(spec);
    GcodeJob job;
    job.machine = machine;
    job.origin = spec.origin;
    job.pixelSizeMm = spec.pixelSizeMm;
    job.transform.widthMm = layout.total.width();
    job.transform.heightMm = layout.total.height();
    job.title = QStringLiteral("Materialtest");
    return job;
}

GcodeResult generateMaterialTestGcode(const MaterialTestSpec& spec, const MachineSetup& machine, const GcodeSink& sink,
                                      const std::atomic_bool* cancel, const std::function<void(double)>& progress)
{
    GcodeResult result;
    const QString problem = validateMaterialTest(spec);
    if (!problem.isEmpty()) {
        result.error = problem;
        return result;
    }
    const MaterialTestLayout layout = materialTestLayout(spec);
    if (!fitsWorkArea(layout.total, machine)) {
        const QSizeF area = workAreaInOperatorView(machine);
        result.error = tr("Der Materialtest (%1 × %2 mm) ist größer als der Arbeitsbereich (%3 × %4 mm).")
                           .arg(layout.total.width(), 0, 'f', 1)
                           .arg(layout.total.height(), 0, 'f', 1)
                           .arg(area.width(), 0, 'f', 0)
                           .arg(area.height(), 0, 'f', 0);
        return result;
    }
    const QPointF fraction = jobOriginFraction(spec.origin);
    const QPointF originInDrawing(fraction.x() * layout.total.width(), fraction.y() * layout.total.height());

    // Teilaufträge: erst die Beschriftung, dann die Felder Zeile für Zeile.
    struct Part {
        GcodeJob job;
    };
    std::vector<Part> parts;
    auto addPart = [&](const QImage& raster, const QRectF& area, EngraveMode mode, double speed, double power) {
        if (raster.isNull()) return;
        Part part;
        GcodeJob& job = part.job;
        job.raster = raster;
        job.pixelSizeMm = spec.pixelSizeMm;
        job.transform.widthMm = raster.width() * spec.pixelSizeMm;
        job.transform.heightMm = raster.height() * spec.pixelSizeMm;
        job.machine = machine;
        job.origin = JobOrigin::TopLeft;
        job.operation.mode = mode;
        job.operation.speedMmPerMin = speed;
        job.operation.powerPercent = power;
        job.operation.passes = 1;
        job.operation.overscanMm = spec.overscanMm;
        job.operation.bidirectional = true;
        job.placed = true;
        // Mittelpunkt des Teils gegenüber dem Ursprung der Zeichnung (Bedienersicht, y nach unten)
        const QPointF center(area.x() + job.transform.widthMm / 2.0, area.y() + job.transform.heightMm / 2.0);
        job.centerFromOriginMm = center - originInDrawing;
        job.title = QStringLiteral("Materialtest");
        parts.push_back(std::move(part));
    };
    for (int i = 0; i < spec.cells && i < static_cast<int>(spec.powerLabels.size()); ++i) {
        addPart(textRaster(spec.powerLabels[static_cast<size_t>(i)], spec.labelHeightMm, spec.pixelSizeMm),
                layout.powerLabels[static_cast<size_t>(i)], EngraveMode::Line, spec.labelSpeedMmPerMin, spec.labelPowerPercent);
    }
    for (int j = 0; j < spec.cells && j < static_cast<int>(spec.speedLabels.size()); ++j) {
        addPart(textRaster(spec.speedLabels[static_cast<size_t>(j)], spec.labelHeightMm, spec.pixelSizeMm),
                layout.speedLabels[static_cast<size_t>(j)], EngraveMode::Line, spec.labelSpeedMmPerMin, spec.labelPowerPercent);
    }
    const QImage motif = motifRaster(spec.motif, spec.sizeMm, spec.pixelSizeMm);
    for (int j = 0; j < spec.cells; ++j) {
        for (int i = 0; i < spec.cells; ++i) {
            addPart(motif, layout.cell(i, j, spec.sizeMm), spec.mode, spec.speeds[static_cast<size_t>(j)],
                    spec.powers[static_cast<size_t>(i)]);
        }
    }
    if (parts.empty()) {
        result.error = tr("Es gibt nichts zu brennen.");
        return result;
    }

    // Zusammensetzen: Kopf nur beim ersten Teil, Ende nur beim letzten. Die Summe der Statistiken ist ein Richtwert.
    GcodeStats total;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        GcodeJob& job = parts[k].job;
        job.omitHeader = k > 0;
        job.omitFooter = k + 1 < parts.size();
        if (k == 0) {
            job.note = QStringLiteral("Materialtest %1 x %2").arg(spec.cells).arg(spec.cells);
        }
        const GcodeResult part = generateGcode(job, sink, cancel);
        if (!part.ok) {
            result.cancelled = part.cancelled;
            result.error = part.error;
            return result;
        }
        total.seconds += part.stats.seconds;
        total.burnLengthMm += part.stats.burnLengthMm;
        total.travelLengthMm += part.stats.travelLengthMm;
        total.lines += part.stats.lines;
        total.bytes += part.stats.bytes;
        if (progress) progress(static_cast<double>(k + 1) / static_cast<double>(parts.size()));
    }
    result.stats = total;
    result.ok = true;
    return result;
}

}  // namespace ld::core
