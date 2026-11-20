#include "core/imageprocessing.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ld::core {

QRect effectiveCrop(const ImageSettings& settings, const QSize& imageSize)
{
    const QRect full(QPoint(0, 0), imageSize);
    if (settings.crop.isEmpty()) {
        return full;
    }
    const QRect clipped = settings.crop.intersected(full);
    return clipped.isEmpty() ? full : clipped;
}

namespace {

bool isCancelled(const std::atomic_bool* cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

unsigned char clampByte(double value)
{
    return static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

}  // namespace

// --------------------------------------------------------------------------- Graustufen
QImage toGrayscale(const QImage& source, const QRect& area, const std::atomic_bool* cancel)
{
    const QRect rect = area.intersected(source.rect());
    if (source.isNull() || rect.isEmpty()) {
        return {};
    }
    const QImage rgb = source.format() == QImage::Format_RGB32 || source.format() == QImage::Format_ARGB32
                           ? source
                           : source.convertToFormat(QImage::Format_RGB32);
    QImage gray(rect.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < rect.height(); ++y) {
        if (isCancelled(cancel)) {
            return {};
        }
        const auto* in = reinterpret_cast<const QRgb*>(rgb.constScanLine(rect.top() + y)) + rect.left();
        unsigned char* out = gray.scanLine(y);
        for (int x = 0; x < rect.width(); ++x) {
            // 77 + 150 + 29 = 256, also entspricht ">> 8" einer Division durch die Gewichtssumme.
            out[x] = static_cast<unsigned char>(
                (77 * qRed(in[x]) + 150 * qGreen(in[x]) + 29 * qBlue(in[x]) + 128) >> 8);
        }
    }
    return gray;
}

// --------------------------------------------------------------------------- Skalieren
namespace {

// Gewichte, mit denen ein Ausgabepixel aus Eingabepixeln gemischt wird.
struct Tap {
    int start = 0;
    std::vector<float> weights;
};

std::vector<Tap> makeTaps(int srcCount, int dstCount)
{
    std::vector<Tap> taps(static_cast<size_t>(dstCount));
    const double scale = static_cast<double>(srcCount) / dstCount;
    for (int i = 0; i < dstCount; ++i) {
        Tap& tap = taps[static_cast<size_t>(i)];
        if (scale > 1.0) {
            // Verkleinern: Mittelwert über das überdeckte Stück (Randpixel anteilig).
            const double a = i * scale;
            const double b = (i + 1) * scale;
            const int first = static_cast<int>(std::floor(a));
            const int last = std::min(srcCount - 1, static_cast<int>(std::ceil(b)) - 1);
            tap.start = first;
            for (int k = first; k <= last; ++k) {
                const double overlap = std::min(b, k + 1.0) - std::max(a, static_cast<double>(k));
                tap.weights.push_back(static_cast<float>(std::max(0.0, overlap) / scale));
            }
        } else {
            // Vergrößern: bilinear zwischen den beiden nächsten Pixelmitten.
            const double center = (i + 0.5) * scale - 0.5;
            const int k0 = static_cast<int>(std::floor(center));
            const double f = center - k0;
            const int a0 = std::clamp(k0, 0, srcCount - 1);
            const int a1 = std::clamp(k0 + 1, 0, srcCount - 1);
            tap.start = a0;
            if (a0 == a1) {
                tap.weights = {1.0f};
            } else {
                tap.weights = {static_cast<float>(1.0 - f), static_cast<float>(f)};
            }
        }
    }
    return taps;
}

}  // namespace

QImage resampleGray(const QImage& gray, const QSize& target, const std::atomic_bool* cancel)
{
    if (gray.isNull() || target.width() < 1 || target.height() < 1) {
        return {};
    }
    if (gray.size() == target) {
        return gray;
    }
    const int srcW = gray.width();
    const int srcH = gray.height();
    const int dstW = target.width();
    const int dstH = target.height();

    // Waagerecht: jede Zeile des Eingangs wird auf dstW Pixel gebracht.
    QImage mid(dstW, srcH, QImage::Format_Grayscale8);
    {
        const std::vector<Tap> taps = makeTaps(srcW, dstW);
        for (int y = 0; y < srcH; ++y) {
            if (isCancelled(cancel)) {
                return {};
            }
            const unsigned char* in = gray.constScanLine(y);
            unsigned char* out = mid.scanLine(y);
            for (int x = 0; x < dstW; ++x) {
                const Tap& tap = taps[static_cast<size_t>(x)];
                float sum = 0.0f;
                for (size_t k = 0; k < tap.weights.size(); ++k) {
                    sum += tap.weights[k] * in[tap.start + static_cast<int>(k)];
                }
                out[x] = clampByte(sum);
            }
        }
    }

    // Senkrecht: jede Ausgabezeile ist eine gewichtete Summe ganzer Eingabezeilen.
    QImage result(dstW, dstH, QImage::Format_Grayscale8);
    {
        const std::vector<Tap> taps = makeTaps(srcH, dstH);
        std::vector<float> accumulator(static_cast<size_t>(dstW));
        for (int y = 0; y < dstH; ++y) {
            if (isCancelled(cancel)) {
                return {};
            }
            std::fill(accumulator.begin(), accumulator.end(), 0.0f);
            const Tap& tap = taps[static_cast<size_t>(y)];
            for (size_t k = 0; k < tap.weights.size(); ++k) {
                const unsigned char* row = mid.constScanLine(tap.start + static_cast<int>(k));
                const float weight = tap.weights[k];
                for (int x = 0; x < dstW; ++x) {
                    accumulator[static_cast<size_t>(x)] += weight * row[x];
                }
            }
            unsigned char* out = result.scanLine(y);
            for (int x = 0; x < dstW; ++x) {
                out[x] = clampByte(accumulator[static_cast<size_t>(x)]);
            }
        }
    }
    return result;
}

// --------------------------------------------------------------------------- Schärfen
QImage sharpenGray(const QImage& gray, int amountPercent, const std::atomic_bool* cancel)
{
    if (gray.isNull() || amountPercent <= 0) {
        return gray;
    }
    const int w = gray.width();
    const int h = gray.height();
    const double amount = amountPercent / 100.0 * 1.5;  // 100 % entspricht Faktor 1,5

    // Weichzeichnen mit [1 2 1]/4 waagerecht, dann senkrecht beim Ausrechnen.
    QImage horizontal(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        if (isCancelled(cancel)) {
            return {};
        }
        const unsigned char* in = gray.constScanLine(y);
        unsigned char* out = horizontal.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int left = in[std::max(0, x - 1)];
            const int right = in[std::min(w - 1, x + 1)];
            out[x] = static_cast<unsigned char>((left + 2 * in[x] + right + 2) >> 2);
        }
    }
    QImage result(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        if (isCancelled(cancel)) {
            return {};
        }
        const unsigned char* above = horizontal.constScanLine(std::max(0, y - 1));
        const unsigned char* here = horizontal.constScanLine(y);
        const unsigned char* below = horizontal.constScanLine(std::min(h - 1, y + 1));
        const unsigned char* original = gray.constScanLine(y);
        unsigned char* out = result.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int blurred = (above[x] + 2 * here[x] + below[x] + 2) >> 2;
            out[x] = clampByte(original[x] + amount * (original[x] - blurred));
        }
    }
    return result;
}

// --------------------------------------------------------------------------- Tonwerte
std::array<unsigned char, 256> toneTable(const ImageSettings& settings)
{
    std::array<unsigned char, 256> table{};
    // Kontrast: übliche Formel mit c im Bereich -255..255 (aus -100..100 hochgerechnet).
    const double c = std::clamp(settings.contrast, -100, 100) * 2.55;
    const double contrastFactor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    const double offset = std::clamp(settings.brightness, -100, 100) * 2.55;
    const double gamma = std::clamp(settings.gamma, 0.2, 3.0);

    for (int v = 0; v < 256; ++v) {
        double x = contrastFactor * (v - 128.0) + 128.0 + offset;
        x = std::clamp(x, 0.0, 255.0);
        x = 255.0 * std::pow(x / 255.0, 1.0 / gamma);
        if (settings.invert) {
            x = 255.0 - x;
        }
        table[static_cast<size_t>(v)] = clampByte(x);
    }
    return table;
}

void applyTable(QImage& gray, const std::array<unsigned char, 256>& table)
{
    for (int y = 0; y < gray.height(); ++y) {
        unsigned char* line = gray.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            line[x] = table[line[x]];
        }
    }
}

// --------------------------------------------------------------------------- Histogramm, Otsu
Histogram histogramOf(const QImage& gray)
{
    Histogram histogram{};
    for (int y = 0; y < gray.height(); ++y) {
        const unsigned char* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            ++histogram[line[x]];
        }
    }
    return histogram;
}

int otsuThreshold(const Histogram& histogram)
{
    long long total = 0;
    double sumAll = 0.0;
    for (int i = 0; i < 256; ++i) {
        total += histogram[static_cast<size_t>(i)];
        sumAll += static_cast<double>(i) * histogram[static_cast<size_t>(i)];
    }
    if (total == 0) {
        return 128;
    }
    // Für jede Trennstelle t (Klasse 0 = Werte 0..t) die Varianz zwischen den Klassen berechnen
    // und die größte nehmen.
    long long weightBackground = 0;
    double sumBackground = 0.0;
    double bestVariance = -1.0;
    int bestT = 0;
    for (int t = 0; t < 256; ++t) {
        weightBackground += histogram[static_cast<size_t>(t)];
        if (weightBackground == 0) {
            continue;
        }
        const long long weightForeground = total - weightBackground;
        if (weightForeground == 0) {
            break;
        }
        sumBackground += static_cast<double>(t) * histogram[static_cast<size_t>(t)];
        const double meanBackground = sumBackground / static_cast<double>(weightBackground);
        const double meanForeground = (sumAll - sumBackground) / static_cast<double>(weightForeground);
        const double diff = meanBackground - meanForeground;
        const double variance = static_cast<double>(weightBackground) *
                                static_cast<double>(weightForeground) * diff * diff;
        if (variance > bestVariance) {
            bestVariance = variance;
            bestT = t;
        }
    }
    // Klasse 0 reicht bis einschließlich bestT; "dunkel" ist deshalb Wert < bestT + 1.
    return std::min(255, bestT + 1);
}

// --------------------------------------------------------------------------- Umwandlung
QImage thresholdGray(const QImage& gray, int threshold)
{
    QImage result(gray.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y) {
        const unsigned char* in = gray.constScanLine(y);
        unsigned char* out = result.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            out[x] = in[x] < threshold ? 0 : 255;
        }
    }
    return result;
}

namespace {

// Ein Nachbar, an den ein Teil des Fehlers weitergegeben wird.
struct DiffusionTap {
    int dx;
    int dy;
    int weight;
};

QImage diffuseError(const QImage& gray, const DiffusionTap* taps, int tapCount, int denominator,
                    const std::atomic_bool* cancel)
{
    const int w = gray.width();
    const int h = gray.height();
    QImage result(w, h, QImage::Format_Grayscale8);

    // Drei Zeilen Fehlerpuffer (aktuelle und zwei folgende), mit Rand links und rechts,
    // damit dx = -1 und dx = +2 nicht aus dem Puffer laufen.
    constexpr int pad = 2;
    std::vector<int> rows[3];
    for (auto& row : rows) {
        row.assign(static_cast<size_t>(w + 2 * pad), 0);
    }
    int current = 0;  // Index der Zeile y im Ringpuffer

    for (int y = 0; y < h; ++y) {
        if (isCancelled(cancel)) {
            return {};
        }
        const unsigned char* in = gray.constScanLine(y);
        unsigned char* out = result.scanLine(y);
        std::vector<int>& errCurrent = rows[current];
        for (int x = 0; x < w; ++x) {
            const int wanted = in[x] + errCurrent[static_cast<size_t>(x + pad)];
            const int value = wanted < 128 ? 0 : 255;
            out[x] = static_cast<unsigned char>(value);
            const int error = wanted - value;
            if (error == 0) {
                continue;
            }
            for (int k = 0; k < tapCount; ++k) {
                const DiffusionTap& tap = taps[k];
                std::vector<int>& target = rows[(current + tap.dy) % 3];
                target[static_cast<size_t>(x + tap.dx + pad)] += error * tap.weight / denominator;
            }
        }
        // Die fertige Zeile leeren und als "übernächste" wiederverwenden.
        std::fill(errCurrent.begin(), errCurrent.end(), 0);
        current = (current + 1) % 3;
    }
    return result;
}

// Bayer-Matrix 8 x 8 (Werte 0..63), rekursiv aus der 2 x 2 Matrix aufgebaut.
std::array<std::array<int, 8>, 8> bayerMatrix()
{
    std::array<std::array<int, 8>, 8> m{};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int value = 0;
            int xs = x;
            int ys = y;
            for (int bit = 0; bit < 3; ++bit) {
                // Bitweises Verschränken von x XOR y und y (klassische Konstruktion).
                value = (value << 2) | (((xs & 1) ^ (ys & 1)) << 1) | (ys & 1);
                xs >>= 1;
                ys >>= 1;
            }
            m[static_cast<size_t>(y)][static_cast<size_t>(x)] = value;
        }
    }
    return m;
}

}  // namespace

QImage ditherGray(const QImage& gray, ConversionMethod method, const std::atomic_bool* cancel)
{
    if (gray.isNull()) {
        return {};
    }
    switch (method) {
    case ConversionMethod::FloydSteinberg: {
        static constexpr DiffusionTap taps[] = {{1, 0, 7}, {-1, 1, 3}, {0, 1, 5}, {1, 1, 1}};
        return diffuseError(gray, taps, 4, 16, cancel);
    }
    case ConversionMethod::Atkinson: {
        // Jeder der sechs Nachbarn bekommt 1/8; 2/8 des Fehlers gehen verloren.
        static constexpr DiffusionTap taps[] = {{1, 0, 1}, {2, 0, 1}, {-1, 1, 1},
                                                {0, 1, 1}, {1, 1, 1}, {0, 2, 1}};
        return diffuseError(gray, taps, 6, 8, cancel);
    }
    case ConversionMethod::Bayer: {
        static const auto matrix = bayerMatrix();
        QImage result(gray.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < gray.height(); ++y) {
            if (isCancelled(cancel)) {
                return {};
            }
            const unsigned char* in = gray.constScanLine(y);
            unsigned char* out = result.scanLine(y);
            for (int x = 0; x < gray.width(); ++x) {
                // Schwelle aus der Matrix: (Wert + 0,5) / 64 * 255
                const double limit = (matrix[static_cast<size_t>(y % 8)][static_cast<size_t>(x % 8)] + 0.5) * 255.0 / 64.0;
                out[x] = in[x] < limit ? 0 : 255;
            }
        }
        return result;
    }
    default:
        return gray;
    }
}

QImage ditherBlocks(const QImage& gray, ConversionMethod method, int cell, const std::atomic_bool* cancel)
{
    cell = std::clamp(cell, 1, 8);
    if (cell == 1 || gray.isNull()) {
        return ditherGray(gray, method, cancel);
    }
    const int w = gray.width();
    const int h = gray.height();
    const int cw = (w + cell - 1) / cell;
    const int ch = (h + cell - 1) / cell;

    // 1) Mittelwert pro Block (Randblöcke sind kleiner).
    QImage coarse(cw, ch, QImage::Format_Grayscale8);
    for (int by = 0; by < ch; ++by) {
        if (isCancelled(cancel)) {
            return {};
        }
        const int y0 = by * cell;
        const int y1 = std::min(h, y0 + cell);
        unsigned char* out = coarse.scanLine(by);
        for (int bx = 0; bx < cw; ++bx) {
            const int x0 = bx * cell;
            const int x1 = std::min(w, x0 + cell);
            long sum = 0;
            for (int y = y0; y < y1; ++y) {
                const unsigned char* in = gray.constScanLine(y);
                for (int x = x0; x < x1; ++x) {
                    sum += in[x];
                }
            }
            const long count = static_cast<long>(y1 - y0) * (x1 - x0);
            out[bx] = static_cast<unsigned char>((sum + count / 2) / count);
        }
    }

    // 2) Dithering auf dem gröberen Raster.
    const QImage dithered = ditherGray(coarse, method, cancel);
    if (dithered.isNull()) {
        return {};
    }

    // 3) Jedes Ergebnis wieder auf einen Block vergrößern.
    QImage result(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        if (isCancelled(cancel)) {
            return {};
        }
        const unsigned char* in = dithered.constScanLine(y / cell);
        unsigned char* out = result.scanLine(y);
        for (int x = 0; x < w; ++x) {
            out[x] = in[x / cell];
        }
    }
    return result;
}

// --------------------------------------------------------------------------- Ganzer Ablauf
QSize rasterSizeFor(const QSizeF& sizeMm, double pixelSizeMm, bool* clamped)
{
    if (clamped) {
        *clamped = false;
    }
    if (sizeMm.width() <= 0.0 || sizeMm.height() <= 0.0 || pixelSizeMm <= 0.0) {
        return {};
    }
    double w = std::max(1.0, std::round(sizeMm.width() / pixelSizeMm));
    double h = std::max(1.0, std::round(sizeMm.height() / pixelSizeMm));
    if (w * h > static_cast<double>(kMaxRasterPixels)) {
        const double factor = std::sqrt(static_cast<double>(kMaxRasterPixels) / (w * h));
        w = std::max(1.0, std::floor(w * factor));
        h = std::max(1.0, std::floor(h * factor));
        if (clamped) {
            *clamped = true;
        }
    }
    return QSize(static_cast<int>(w), static_cast<int>(h));
}

ProcessResult processImage(const QImage& source, const ImageSettings& settings, const QSizeF& sizeMm,
                           const std::atomic_bool* cancel)
{
    ProcessResult result;
    bool clamped = false;
    const QSize target = rasterSizeFor(sizeMm, settings.pixelSizeMm, &clamped);
    if (source.isNull() || target.isEmpty()) {
        return result;
    }
    result.clamped = clamped;

    auto cancelled = [&]() {
        if (isCancelled(cancel)) {
            result = ProcessResult{};
            result.cancelled = true;
            return true;
        }
        return false;
    };

    QImage gray = toGrayscale(source, effectiveCrop(settings, source.size()), cancel);
    if (cancelled()) return result;
    gray = resampleGray(gray, target, cancel);
    if (cancelled()) return result;
    result.originalHistogram = histogramOf(gray);   // so sieht das Bild vor allen Änderungen aus
    gray = sharpenGray(gray, settings.sharpen, cancel);
    if (cancelled()) return result;
    if (gray.isNull()) {
        return result;
    }
    // Falls resampleGray/sharpenGray dasselbe (geteilte) Bild zurückgaben, vor dem Ändern kopieren.
    gray.detach();
    applyTable(gray, toneTable(settings));

    result.histogram = histogramOf(gray);
    result.otsu = otsuThreshold(result.histogram);
    result.usedThreshold = settings.autoThreshold ? result.otsu : std::clamp(settings.threshold, 0, 255);

    switch (settings.method) {
    case ConversionMethod::Threshold:
        result.raster = thresholdGray(gray, result.usedThreshold);
        break;
    case ConversionMethod::FloydSteinberg:
    case ConversionMethod::Atkinson:
    case ConversionMethod::Bayer:
        result.raster = ditherBlocks(gray, settings.method, settings.ditherCell, cancel);
        break;
    case ConversionMethod::Grayscale:
        result.raster = gray;
        break;
    }
    if (cancelled()) return result;
    return result;
}

QRect contentBounds(const QImage& source, const ImageSettings& settings, bool binaryOnly)
{
    if (source.isNull()) {
        return {};
    }
    ImageSettings analysis = settings;
    analysis.crop = QRect();   // das ganze Bild
    if (binaryOnly) {
        analysis.method = ConversionMethod::Threshold;
    }
    // Arbeitsraster in der Größe des Originals: Ein Rasterpixel entspricht einem Quellpixel.
    const double pixel = analysis.pixelSizeMm > 0.0 ? analysis.pixelSizeMm : 1.0;
    analysis.pixelSizeMm = pixel;
    const ProcessResult result = processImage(source, analysis, QSizeF(source.width() * pixel, source.height() * pixel));
    const QImage& raster = result.raster;
    if (raster.isNull() || raster.format() != QImage::Format_Grayscale8) {
        return {};
    }
    int left = raster.width(), right = -1, top = raster.height(), bottom = -1;
    for (int y = 0; y < raster.height(); ++y) {
        const uchar* row = raster.constScanLine(y);
        for (int x = 0; x < raster.width(); ++x) {
            if (row[x] < 255) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < 0) {
        return {};   // nichts wird gebrannt
    }
    // Zurück in Pixel des Originals (gleich, wenn das Raster nicht verkleinert werden musste).
    const double fx = static_cast<double>(source.width()) / raster.width();
    const double fy = static_cast<double>(source.height()) / raster.height();
    const int x0 = std::clamp(static_cast<int>(std::floor(left * fx)), 0, source.width() - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(top * fy)), 0, source.height() - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil((right + 1) * fx)), x0 + 1, source.width());
    const int y1 = std::clamp(static_cast<int>(std::ceil((bottom + 1) * fy)), y0 + 1, source.height());
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

ObjectTransform transformForCrop(const ObjectTransform& base, const QRect& baseCrop,
                                 const QRect& newCrop, double percent)
{
    ObjectTransform t = base;
    if (baseCrop.width() > 0 && baseCrop.height() > 0) {
        t.widthMm = base.widthMm * static_cast<double>(newCrop.width()) / baseCrop.width();
        t.heightMm = base.heightMm * static_cast<double>(newCrop.height()) / baseCrop.height();
    }
    t.widthMm *= percent / 100.0;
    t.heightMm *= percent / 100.0;
    return t;
}

}  // namespace ld::core
