#include "core/calibration.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "core/materialtest.h"

namespace ld::core {

namespace {

constexpr double kMargin = 0.6;          // Rand um die Linien (mm), damit nichts abgeschnitten wird
constexpr double kLabelHeightMm = 3.0;   // Höhe der Zahlen
constexpr double kShortTick = 2.0;
constexpr double kLongTick = 4.0;        // alle 50 mm
constexpr double kCircleK = 0.5522847498307936;   // Bézier-Konstante für den Viertelkreis

using Polyline = std::vector<QPointF>;

// Strecke als kubischer Bézierzug mit Stützpunkten auf den Enden (wie beim Text).
SvgSubpath fromPolyline(const Polyline& line)
{
    SvgSubpath sub;
    if (line.size() < 2) return sub;
    sub.bezier.push_back(line[0]);
    for (size_t i = 1; i < line.size(); ++i) {
        sub.bezier.push_back(line[i - 1]);
        sub.bezier.push_back(line[i]);
        sub.bezier.push_back(line[i]);
    }
    sub.closed = false;
    return sub;
}

QPainterPath toPath(const SvgSubpath& sub)
{
    QPainterPath path;
    if (sub.bezier.size() < 4) return path;
    path.moveTo(sub.bezier[0]);
    for (size_t i = 1; i + 2 < sub.bezier.size(); i += 3) {
        path.cubicTo(sub.bezier[i], sub.bezier[i + 1], sub.bezier[i + 2]);
    }
    return path;
}

}  // namespace

CalibrationSpec sanitized(const CalibrationSpec& in)
{
    CalibrationSpec s = in;
    s.lengthXMm = std::clamp(s.lengthXMm, kCalibrationMinMm, kCalibrationMaxMm);
    s.lengthYMm = std::clamp(s.lengthYMm, kCalibrationMinMm, kCalibrationMaxMm);
    s.circleDiameterMm = std::clamp(s.circleDiameterMm, 10.0, kCalibrationMaxMm);
    return s;
}

CalibrationShape layoutCalibration(const CalibrationSpec& specIn)
{
    const CalibrationSpec spec = sanitized(specIn);
    const double lx = spec.lengthXMm, ly = spec.lengthYMm;
    std::vector<SvgSubpath> strokes;
    auto addLine = [&](const Polyline& line) { strokes.push_back(fromPolyline(line)); };

    // Achsen: Eckpunkt unten links bei (0, ly); die X-Achse läuft nach rechts, die Y-Achse nach oben.
    addLine({{0.0, ly}, {lx, ly}});
    addLine({{0.0, ly}, {0.0, 0.0}});

    // Teilstriche (nach außen) und Zahlen alle 50 mm.
    auto addLabel = [&](const QString& text, double left, double top) {
        for (Polyline line : textStrokes(text, kLabelHeightMm)) {
            for (QPointF& p : line) p += QPointF(left, top);
            addLine(line);
        }
    };
    auto labelWidth = [&](const QString& text) { return (text.size() * 3.0 - 1.0) * kLabelHeightMm / 4.0; };
    for (double v = kCalibrationTickMm; v <= lx + 1e-6; v += kCalibrationTickMm) {
        const bool longTick = std::fmod(v, 50.0) < 1e-6;
        const double len = longTick ? kLongTick : kShortTick;
        addLine({{v, ly}, {v, ly + len}});
        if (longTick) {
            const QString text = QString::number(static_cast<int>(std::lround(v)));
            addLabel(text, v - labelWidth(text) / 2.0, ly + kLongTick + 1.0);
        }
    }
    for (double v = kCalibrationTickMm; v <= ly + 1e-6; v += kCalibrationTickMm) {
        const bool longTick = std::fmod(v, 50.0) < 1e-6;
        const double len = longTick ? kLongTick : kShortTick;
        addLine({{0.0, ly - v}, {-len, ly - v}});
        if (longTick) {
            const QString text = QString::number(static_cast<int>(std::lround(v)));
            addLabel(text, -kLongTick - 1.0 - labelWidth(text), ly - v - kLabelHeightMm / 2.0);
        }
    }
    // Achsenbuchstaben an den Enden.
    addLabel(QStringLiteral("X"), lx + 2.5, ly - kLabelHeightMm / 2.0);
    addLabel(QStringLiteral("Y"), -labelWidth(QStringLiteral("Y")) / 2.0, -kLabelHeightMm - 2.5);

    // Kreis: Mittelpunkt auf der Hälfte beider Achsen.
    {
        const QPointF c(lx / 2.0, ly / 2.0);
        const double r = spec.circleDiameterMm / 2.0;
        const double k = kCircleK * r;
        SvgSubpath circle;
        circle.closed = true;
        circle.bezier = {
            {c.x() + r, c.y()},
            {c.x() + r, c.y() + k}, {c.x() + k, c.y() + r}, {c.x(), c.y() + r},
            {c.x() - k, c.y() + r}, {c.x() - r, c.y() + k}, {c.x() - r, c.y()},
            {c.x() - r, c.y() - k}, {c.x() - k, c.y() - r}, {c.x(), c.y() - r},
            {c.x() + k, c.y() - r}, {c.x() + r, c.y() - k}, {c.x() + r, c.y()},
        };
        strokes.push_back(std::move(circle));
    }

    // Umschließendes Rechteck bestimmen und alles an die obere linke Ecke (plus Rand) schieben.
    QPainterPath all;
    for (const SvgSubpath& s : strokes) all.addPath(toPath(s));
    const QRectF bounds = all.boundingRect().adjusted(-kMargin, -kMargin, kMargin, kMargin);
    CalibrationShape shape;
    const QPointF shift = -bounds.topLeft();
    for (SvgSubpath& s : strokes) {
        for (QPointF& p : s.bezier) p += shift;
    }
    shape.strokes = std::move(strokes);
    for (const SvgSubpath& s : shape.strokes) shape.path.addPath(toPath(s));
    shape.sizeMm = bounds.size();
    shape.cornerMm = QPointF(0.0, ly) + shift;
    return shape;
}

std::shared_ptr<const SvgModel> calibrationToSvgModel(const CalibrationShape& shape)
{
    if (shape.isEmpty()) return nullptr;
    constexpr double toPx = 96.0 / 25.4;
    auto model = std::make_shared<SvgModel>();
    model->width = shape.sizeMm.width() * toPx;
    model->height = shape.sizeMm.height() * toPx;
    SvgShape svgShape;
    svgShape.hasFill = false;
    svgShape.hasStroke = true;
    svgShape.stroke = Qt::black;
    svgShape.strokeWidth = kCalibrationLineWidthMm * toPx;
    for (SvgSubpath sub : shape.strokes) {
        for (QPointF& p : sub.bezier) p *= toPx;
        svgShape.subpaths.push_back(std::move(sub));
    }
    model->shapes.push_back(std::move(svgShape));
    return model;
}

QImage renderCalibrationRaster(const CalibrationShape& shape, double pixelSizeMm)
{
    if (shape.isEmpty() || pixelSizeMm <= 0.0) return {};
    double w = std::max(1.0, std::round(shape.sizeMm.width() / pixelSizeMm));
    double h = std::max(1.0, std::round(shape.sizeMm.height() / pixelSizeMm));
    const double ratio = w * h / 64.0e6;
    if (ratio > 1.0) {
        w = std::max(1.0, std::floor(w / std::sqrt(ratio)));
        h = std::max(1.0, std::floor(h / std::sqrt(ratio)));
    }
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(w / shape.sizeMm.width(), h / shape.sizeMm.height());
    QPen pen(Qt::black, std::max(kCalibrationLineWidthMm, pixelSizeMm), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape.path);
    painter.end();
    return image;
}

CalibrationContent makeCalibrationContent(const CalibrationSpec& spec, double pixelSizeMm)
{
    CalibrationContent content;
    content.shape = layoutCalibration(spec);
    content.image = renderCalibrationRaster(content.shape, pixelSizeMm);
    content.svg = calibrationToSvgModel(content.shape);
    return content;
}

QJsonObject calibrationSpecToJson(const CalibrationSpec& spec)
{
    QJsonObject o;
    o[QStringLiteral("lengthXMm")] = spec.lengthXMm;
    o[QStringLiteral("lengthYMm")] = spec.lengthYMm;
    o[QStringLiteral("circleDiameterMm")] = spec.circleDiameterMm;
    return o;
}

CalibrationSpec calibrationSpecFromJson(const QJsonObject& o)
{
    CalibrationSpec s;
    s.lengthXMm = o.value(QStringLiteral("lengthXMm")).toDouble(s.lengthXMm);
    s.lengthYMm = o.value(QStringLiteral("lengthYMm")).toDouble(s.lengthYMm);
    s.circleDiameterMm = o.value(QStringLiteral("circleDiameterMm")).toDouble(s.circleDiameterMm);
    return sanitized(s);
}

std::optional<double> calibrationFactor(double nominalAxisMm, double measuredAxisMm, double nominalCircleMm,
                                        double measuredCircleMm)
{
    double sum = 0.0;
    int count = 0;
    if (measuredAxisMm > 0.0 && nominalAxisMm > 0.0) {
        sum += nominalAxisMm / measuredAxisMm;
        ++count;
    }
    if (measuredCircleMm > 0.0 && nominalCircleMm > 0.0) {
        sum += nominalCircleMm / measuredCircleMm;
        ++count;
    }
    if (count == 0) return std::nullopt;
    return std::clamp(sum / count, 0.5, 2.0);
}

}  // namespace ld::core
