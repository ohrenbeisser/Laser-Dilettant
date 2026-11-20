#include "core/svgmodel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstring>

// nanosvg ist eine einzelne Header-Datei (zlib-Lizenz); die Implementierung wird hier eingebunden.
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"

namespace ld::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

// nanosvg speichert Farben als 0xAABBGGRR.
QColor colorFromNano(unsigned int c)
{
    return QColor(static_cast<int>(c & 0xff), static_cast<int>((c >> 8) & 0xff), static_cast<int>((c >> 16) & 0xff),
                  static_cast<int>((c >> 24) & 0xff));
}

bool isDark(const QColor& c)
{
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue() < 128.0;
}

// Deckend genug (mindestens 50 %), sonst wird der Teil ignoriert.
bool isOpaque(const QColor& c)
{
    return c.alpha() >= 128;
}

QPainterPath toPainterPath(const SvgShape& shape)
{
    QPainterPath path;
    path.setFillRule(shape.fillEvenOdd ? Qt::OddEvenFill : Qt::WindingFill);
    for (const SvgSubpath& sub : shape.subpaths) {
        if (sub.bezier.size() < 4) continue;
        path.moveTo(sub.bezier[0]);
        for (size_t i = 1; i + 2 < sub.bezier.size(); i += 3) {
            path.cubicTo(sub.bezier[i], sub.bezier[i + 1], sub.bezier[i + 2]);
        }
        if (sub.closed) path.closeSubpath();
    }
    return path;
}

// Bézierkurve in Strecken zerlegen (de Casteljau, Abweichung höchstens tolerance).
double lineDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len = std::hypot(dx, dy);
    if (len < 1e-12) return std::hypot(p.x() - a.x(), p.y() - a.y());
    return std::abs((p.x() - a.x()) * dy - (p.y() - a.y()) * dx) / len;
}

void flatten(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double tol, int depth,
             std::vector<QPointF>& out)
{
    if (depth >= 16 || std::max(lineDistance(p1, p0, p3), lineDistance(p2, p0, p3)) <= tol) {
        out.push_back(p3);
        return;
    }
    const QPointF p01 = (p0 + p1) / 2, p12 = (p1 + p2) / 2, p23 = (p2 + p3) / 2;
    const QPointF p012 = (p01 + p12) / 2, p123 = (p12 + p23) / 2, mid = (p012 + p123) / 2;
    flatten(p0, p01, p012, mid, tol, depth + 1, out);
    flatten(mid, p123, p23, p3, tol, depth + 1, out);
}

}  // namespace

std::shared_ptr<const SvgModel> parseSvgFile(const QString& path, QString* error)
{
    auto fail = [&](const QString& message) -> std::shared_ptr<const SvgModel> {
        if (error) *error = message;
        return nullptr;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(tr("Die SVG-Datei konnte nicht geöffnet werden."));
    }
    QByteArray data = file.readAll();
    auto model = std::make_shared<SvgModel>();
    model->hasText = data.contains("<text");
    data.append('\0');   // nanosvg braucht einen nullterminierten Text und verändert ihn
    NSVGimage* image = nsvgParse(data.data(), "px", 96.0f);
    if (!image) {
        return fail(tr("Die SVG-Datei konnte nicht gelesen werden."));
    }
    model->width = image->width;
    model->height = image->height;
    for (NSVGshape* s = image->shapes; s; s = s->next) {
        if (!(s->flags & NSVG_FLAGS_VISIBLE)) continue;
        SvgShape shape;
        shape.fillEvenOdd = s->fillRule == NSVG_FILLRULE_EVENODD;
        if (s->fill.type == NSVG_PAINT_COLOR) {
            shape.hasFill = true;
            shape.fill = colorFromNano(s->fill.color);
            shape.fill.setAlpha(static_cast<int>(shape.fill.alpha() * s->opacity));
        }
        if (s->stroke.type == NSVG_PAINT_COLOR && s->strokeWidth > 0.0f) {
            shape.hasStroke = true;
            shape.stroke = colorFromNano(s->stroke.color);
            shape.stroke.setAlpha(static_cast<int>(shape.stroke.alpha() * s->opacity));
            shape.strokeWidth = s->strokeWidth;
            shape.join = s->strokeLineJoin == NSVG_JOIN_ROUND   ? Qt::RoundJoin
                         : s->strokeLineJoin == NSVG_JOIN_BEVEL ? Qt::BevelJoin
                                                                : Qt::MiterJoin;
            shape.cap = s->strokeLineCap == NSVG_CAP_ROUND    ? Qt::RoundCap
                        : s->strokeLineCap == NSVG_CAP_SQUARE ? Qt::SquareCap
                                                              : Qt::FlatCap;
            shape.miterLimit = s->miterLimit;
        }
        for (NSVGpath* p = s->paths; p; p = p->next) {
            SvgSubpath sub;
            sub.closed = p->closed != 0;
            for (int i = 0; i < p->npts; ++i) {
                sub.bezier.emplace_back(p->pts[i * 2], p->pts[i * 2 + 1]);
            }
            if (sub.bezier.size() >= 4) shape.subpaths.push_back(std::move(sub));
        }
        if (!shape.subpaths.empty() && (shape.hasFill || shape.hasStroke)) {
            model->shapes.push_back(std::move(shape));
        }
    }
    nsvgDelete(image);
    if (model->width <= 0.0 || model->height <= 0.0) {
        return fail(tr("Die SVG-Datei hat keine gültige Größe."));
    }
    return model;
}

QImage renderSvgFill(const SvgModel& model, bool strokesAsArea, const QSize& size)
{
    if (size.isEmpty() || model.width <= 0.0 || model.height <= 0.0) return {};
    QImage image(size, QImage::Format_Grayscale8);
    image.fill(255);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size.width() / model.width, size.height() / model.height);
    painter.setPen(Qt::NoPen);
    for (const SvgShape& shape : model.shapes) {
        const QPainterPath path = toPainterPath(shape);
        if (shape.hasFill && isOpaque(shape.fill)) {
            painter.fillPath(path, isDark(shape.fill) ? Qt::black : Qt::white);   // hell = Papier
        }
        if (strokesAsArea && shape.hasStroke && isOpaque(shape.stroke)) {
            QPen pen(isDark(shape.stroke) ? Qt::black : Qt::white, shape.strokeWidth, Qt::SolidLine, shape.cap,
                     shape.join);
            pen.setMiterLimit(shape.miterLimit);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            painter.setPen(Qt::NoPen);
        }
    }
    painter.end();
    return image;
}

std::vector<Contour> svgCutContours(const SvgModel& model, const SvgSettings& settings, double toleranceMm,
                                    double widthMm, const QSizeF& space)
{
    std::vector<Contour> out;
    if (model.width <= 0.0 || model.height <= 0.0 || widthMm <= 0.0) return out;
    const double tolUnits = std::max(1e-6, toleranceMm / (widthMm / model.width));
    const double sx = space.width() / model.width;
    const double sy = space.height() / model.height;

    auto addSubpath = [&](const SvgSubpath& sub, bool closedForced) {
        Contour c;
        c.closed = closedForced || sub.closed;
        std::vector<QPointF> pts{sub.bezier[0]};
        for (size_t i = 1; i + 2 < sub.bezier.size(); i += 3) {
            flatten(sub.bezier[i - 1], sub.bezier[i], sub.bezier[i + 1], sub.bezier[i + 2], tolUnits, 0, pts);
        }
        for (const QPointF& p : pts) {
            const QPointF q(p.x() * sx, p.y() * sy);
            if (c.points.empty() || std::hypot(q.x() - c.points.back().x(), q.y() - c.points.back().y()) > 1e-9) {
                c.points.push_back(q);
            }
        }
        // Geschlossen: ein Schlusspunkt gleich dem ersten ist überflüssig.
        while (c.closed && c.points.size() > 1 &&
               std::hypot(c.points.front().x() - c.points.back().x(), c.points.front().y() - c.points.back().y()) <= 1e-9) {
            c.points.pop_back();
        }
        if (c.points.size() >= (c.closed ? 3u : 2u)) out.push_back(std::move(c));
    };

    for (const SvgShape& shape : model.shapes) {
        if (settings.cutStrokes && shape.hasStroke && isOpaque(shape.stroke) && isDark(shape.stroke)) {
            for (const SvgSubpath& sub : shape.subpaths) addSubpath(sub, false);
        } else if (settings.cutFills && shape.hasFill && isOpaque(shape.fill) && isDark(shape.fill)) {
            for (const SvgSubpath& sub : shape.subpaths) addSubpath(sub, true);
        }
    }
    assignNesting(out);
    return out;
}

}  // namespace ld::core
