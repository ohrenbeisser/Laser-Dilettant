#include "core/textmodel.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace ld::core {

namespace {
constexpr double kFontPixels = 1000.0;   // Rechengröße der Schrift; später in mm umgerechnet
}  // namespace

TextSpec sanitized(const TextSpec& in)
{
    TextSpec s = in;
    s.heightMm = std::clamp(s.heightMm, kTextMinHeightMm, kTextMaxHeightMm);
    s.letterSpacingPercent = std::clamp(s.letterSpacingPercent, kTextMinSpacingPercent, kTextMaxSpacingPercent);
    s.lineSpacing = std::clamp(s.lineSpacing, kTextMinLineSpacing, kTextMaxLineSpacing);
    if (s.align != TextAlign::Center && s.align != TextAlign::Right) {
        s.align = TextAlign::Left;
    }
    return s;
}

TextShape layoutText(const TextSpec& specIn)
{
    const TextSpec spec = sanitized(specIn);
    TextShape shape;

    QFont font = spec.fontFamily.isEmpty() ? QFont() : QFont(spec.fontFamily);
    font.setPixelSize(static_cast<int>(kFontPixels));
    font.setBold(spec.bold);
    font.setItalic(spec.italic);
    font.setHintingPreference(QFont::PreferNoHinting);   // keine Anpassung an Bildschirmpixel
    font.setKerning(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spec.letterSpacingPercent / 100.0 * kFontPixels);

    const QFontMetricsF metrics(font);
    const double capHeight = metrics.capHeight() > 1.0 ? metrics.capHeight() : metrics.ascent() * 0.7;
    const double scale = spec.heightMm / capHeight;   // mm pro Rechenpixel
    const double lineStep = metrics.lineSpacing() * spec.lineSpacing;

    // Zeilen trennen (alle gängigen Umbrüche); die letzte leere Zeile zählt mit.
    QString normalized = spec.text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    normalized.replace(QChar(0x2028), QLatin1Char('\n')).replace(QChar(0x2029), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    // Erst messen: Breite jeder Zeile (für die Ausrichtung).
    struct Line { std::unique_ptr<QTextLayout> layout; double width = 0.0; };
    std::vector<Line> laid;
    double maxWidth = 0.0;
    for (const QString& text : lines) {
        Line line;
        line.layout = std::make_unique<QTextLayout>(text, font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        line.layout->setTextOption(option);
        line.layout->beginLayout();
        QTextLine tl = line.layout->createLine();
        if (tl.isValid()) {
            tl.setLineWidth(1.0e7);
            line.width = tl.naturalTextWidth();
        }
        line.layout->endLayout();
        maxWidth = std::max(maxWidth, line.width);
        laid.push_back(std::move(line));
    }

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (size_t i = 0; i < laid.size(); ++i) {
        const Line& line = laid[i];
        double x = 0.0;
        if (spec.align == TextAlign::Center) {
            x = (maxWidth - line.width) / 2.0;
        } else if (spec.align == TextAlign::Right) {
            x = maxWidth - line.width;
        }
        const double y = static_cast<double>(i) * lineStep;   // Oberkante der Zeile
        for (const QGlyphRun& run : line.layout->glyphRuns()) {
            const QRawFont raw = run.rawFont();
            const QList<quint32> glyphs = run.glyphIndexes();
            const QList<QPointF> positions = run.positions();
            for (int g = 0; g < glyphs.size(); ++g) {
                QPainterPath glyphPath = raw.pathForGlyph(glyphs[g]);
                if (glyphPath.isEmpty()) {
                    continue;   // Leerzeichen
                }
                glyphPath.translate(positions[g] + QPointF(x, y));
                path.addPath(glyphPath);
            }
        }
    }
    if (path.isEmpty()) {
        return shape;
    }
    // In mm umrechnen und an die obere linke Ecke der Umrisse legen.
    QTransform toMm;
    toMm.scale(scale, scale);
    path = toMm.map(path);
    const QRectF bounds = path.boundingRect();
    path.translate(-bounds.topLeft());
    shape.path = path;
    shape.sizeMm = bounds.size();
    return shape;
}

std::shared_ptr<const SvgModel> textToSvgModel(const TextShape& shape)
{
    if (shape.isEmpty()) {
        return nullptr;
    }
    auto model = std::make_shared<SvgModel>();
    model->width = shape.sizeMm.width() * 96.0 / 25.4;
    model->height = shape.sizeMm.height() * 96.0 / 25.4;
    const double toPx = 96.0 / 25.4;

    SvgShape svgShape;
    svgShape.hasFill = true;
    svgShape.fill = Qt::black;
    svgShape.fillEvenOdd = false;
    // Pfadelemente der Reihe nach in kubische Bézierstücke (3n+1 Punkte) umsetzen.
    SvgSubpath current;
    QPointF last;
    auto flush = [&] {
        if (current.bezier.size() >= 4) {
            // geschlossen: das Ende liegt auf dem Anfang
            if (current.bezier.back() != current.bezier.front()) {
                const QPointF a = current.bezier.back();
                const QPointF b = current.bezier.front();
                current.bezier.push_back(a);
                current.bezier.push_back(b);
                current.bezier.push_back(b);
            }
            current.closed = true;
            svgShape.subpaths.push_back(std::move(current));
        }
        current = SvgSubpath{};
    };
    auto addPoint = [&](const QPointF& p) { current.bezier.push_back(p * toPx); };
    const int count = shape.path.elementCount();
    for (int i = 0; i < count; ++i) {
        const QPainterPath::Element e = shape.path.elementAt(i);
        const QPointF p(e.x, e.y);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            flush();
            addPoint(p);
            last = p;
            break;
        case QPainterPath::LineToElement:
            // Strecke als Kurve mit Stützpunkten auf den Enden
            addPoint(last);
            addPoint(p);
            addPoint(p);
            last = p;
            break;
        case QPainterPath::CurveToElement: {
            const QPointF c2(shape.path.elementAt(i + 1).x, shape.path.elementAt(i + 1).y);
            const QPointF end(shape.path.elementAt(i + 2).x, shape.path.elementAt(i + 2).y);
            addPoint(p);
            addPoint(c2);
            addPoint(end);
            last = end;
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;   // wird oben mit dem CurveToElement behandelt
        }
    }
    flush();
    model->shapes.push_back(std::move(svgShape));
    return model;
}

QImage renderTextRaster(const TextShape& shape, double pixelSizeMm, double maxPixels)
{
    if (shape.isEmpty() || pixelSizeMm <= 0.0) {
        return {};
    }
    double w = std::max(1.0, std::round(shape.sizeMm.width() / pixelSizeMm));
    double h = std::max(1.0, std::round(shape.sizeMm.height() / pixelSizeMm));
    const double ratio = w * h / maxPixels;
    if (ratio > 1.0) {
        w = std::max(1.0, std::floor(w / std::sqrt(ratio)));
        h = std::max(1.0, std::floor(h / std::sqrt(ratio)));
    }
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(w / shape.sizeMm.width(), h / shape.sizeMm.height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawPath(shape.path);
    painter.end();
    return image;
}

TextContent makeTextContent(const TextSpec& spec, double pixelSizeMm)
{
    TextContent content;
    content.shape = layoutText(spec);
    content.image = renderTextRaster(content.shape, pixelSizeMm);
    content.svg = textToSvgModel(content.shape);
    return content;
}

QJsonObject textSpecToJson(const TextSpec& spec)
{
    QJsonObject o;
    o[QStringLiteral("text")] = spec.text;
    o[QStringLiteral("fontFamily")] = spec.fontFamily;
    o[QStringLiteral("bold")] = spec.bold;
    o[QStringLiteral("italic")] = spec.italic;
    o[QStringLiteral("heightMm")] = spec.heightMm;
    o[QStringLiteral("letterSpacingPercent")] = spec.letterSpacingPercent;
    o[QStringLiteral("lineSpacing")] = spec.lineSpacing;
    o[QStringLiteral("align")] = static_cast<int>(spec.align);
    return o;
}

TextSpec textSpecFromJson(const QJsonObject& o)
{
    TextSpec s;
    s.text = o.value(QStringLiteral("text")).toString();
    s.fontFamily = o.value(QStringLiteral("fontFamily")).toString();
    s.bold = o.value(QStringLiteral("bold")).toBool(false);
    s.italic = o.value(QStringLiteral("italic")).toBool(false);
    s.heightMm = o.value(QStringLiteral("heightMm")).toDouble(s.heightMm);
    s.letterSpacingPercent = o.value(QStringLiteral("letterSpacingPercent")).toDouble(0.0);
    s.lineSpacing = o.value(QStringLiteral("lineSpacing")).toDouble(1.0);
    s.align = static_cast<TextAlign>(o.value(QStringLiteral("align")).toInt(0));
    return sanitized(s);
}

}  // namespace ld::core
