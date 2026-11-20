#include "core/imageloader.h"

#include <QCoreApplication>
#include <QImageReader>
#include <QPainter>
#include <QFileInfo>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace ld::core {

QStringList supportedImageSuffixes()
{
    // Die Qt-Formatnamen sind nicht immer die Endungen: "jpeg" -> jpg und jpeg, "tiff" -> tif und tiff.
    QStringList suffixes{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"),
                         QStringLiteral("svg")};
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        const QString name = QString::fromLatin1(format).toLower();
        QStringList more{name};
        if (name == QLatin1String("tiff")) more = {QStringLiteral("tif"), QStringLiteral("tiff")};
        if (name == QLatin1String("jpeg")) more = {QStringLiteral("jpg"), QStringLiteral("jpeg")};
        if (name == QLatin1String("wbmp")) continue;   // Mini-Schwarzweißformat der alten Handys, nie gebraucht
        for (const QString& s : more) {
            if (!suffixes.contains(s)) suffixes << s;
        }
    }
    // jpg, jpeg und png stehen immer drin; fehlt das Plugin, liefert der Ladevorgang eine klare Fehlermeldung.
    return suffixes;
}

QImage flattenOnWhite(const QImage& source)
{
    if (source.isNull()) {
        return {};
    }
    if (!source.hasAlphaChannel()) {
        return source.convertToFormat(QImage::Format_RGB32);
    }
    QImage result(source.size(), QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter painter(&result);
    painter.drawImage(0, 0, source);  // Alpha wird dabei über das Weiß gemischt
    painter.end();
    return result;
}

LoadResult loadImageFile(const QString& path, double pixelSizeMm)
{
    if (QFileInfo(path).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0 && pixelSizeMm > 0.0) {
        QString parseError;
        std::shared_ptr<const SvgModel> model = parseSvgFile(path, &parseError);
        QSvgRenderer renderer(path);
        if (model && renderer.isValid()) {
            // Größe in Rasterpixeln; bei mehr als 64 Mpx kleiner zeichnen (wie beim Arbeitsraster).
            const QSizeF mm = model->sizeMm();
            double w = std::max(1.0, std::round(mm.width() / pixelSizeMm));
            double h = std::max(1.0, std::round(mm.height() / pixelSizeMm));
            const double mpx = w * h / 64.0e6;
            if (mpx > 1.0) {
                w = std::max(1.0, std::floor(w / std::sqrt(mpx)));
                h = std::max(1.0, std::floor(h / std::sqrt(mpx)));
            }
            QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            renderer.render(&painter, QRectF(0, 0, w, h));
            painter.end();
            return {image, {}, model};
        }
        // nanosvg oder Qt kommt mit der Datei nicht zurecht: wie ein Rasterbild versuchen (ohne Vektordaten).
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);  // EXIF-Ausrichtung anwenden
    const QImage image = reader.read();
    if (image.isNull()) {
        return {{}, QCoreApplication::translate("ld::core", "Das Bild konnte nicht geladen werden: %1")
                        .arg(reader.errorString()), nullptr};
    }
    return {flattenOnWhite(image), {}, nullptr};
}

}  // namespace ld::core
