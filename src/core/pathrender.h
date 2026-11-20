// Zeichnet die Laserwege (PathData) in ein Bild und rechnet das im Hintergrund.
#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QThreadPool>
#include <QTimer>
#include <QTransform>
#include <atomic>
#include <memory>

#include "core/pathdata.h"

namespace ld::core {

// Feste Standardfarben (einstellbar erst mit den Programmeinstellungen in M7, siehe planung.md 3.11).
struct PathRenderOptions {
    bool showBurn = true;
    bool showTravel = true;
    bool showRapid = false;
    double burnWidthMm = 0.0635;                 // Breite der Gravurspur (Pixelgröße)
    double burnMinWidthPx = 1.0;                 // Mindestbreite auf dem Bildschirm (Cutting: breiter, damit sie auffällt)
    QColor burnColor = QColor(0xa0, 0x18, 0x18);
    QColor travelColor = QColor(0x9a, 0xa0, 0xa6);
    QColor rapidColor = QColor(0xe0, 0xa8, 0x00);
};

// Zeichnet in "image" (wird nicht gelöscht). sceneToImage rechnet Szene-mm in Bildpixel um.
// Es werden nur Wegstücke gezeichnet, die im Bild sichtbar sind. false bei Abbruch.
bool renderPaths(const PathData& data, QImage& image, const QTransform& sceneToImage,
                 const PathRenderOptions& options, const std::atomic_bool* cancel = nullptr);

// Rechnet renderPaths() im Hintergrund. Eine neue Anfrage bricht die laufende ab.
class PathRenderRunner : public QObject {
    Q_OBJECT

public:
    explicit PathRenderRunner(int debounceMs = 120, QObject* parent = nullptr);
    ~PathRenderRunner() override;

    // Bild der Größe imageSize (transparent), in das die Wege mit sceneToImage gezeichnet werden.
    // sceneRect ist der Ausschnitt der Szene, den das Bild zeigt (kommt mit dem Ergebnis zurück).
    void request(std::shared_ptr<const PathData> data, const QSize& imageSize, const QTransform& sceneToImage,
                 const QRectF& sceneRect, const PathRenderOptions& options);
    void cancel();
    bool isBusy() const { return busy_; }

signals:
    void finished(const QImage& image, const QRectF& sceneRect);

private:
    void startJob();

    struct Pending {
        std::shared_ptr<const PathData> data;
        QSize size;
        QTransform transform;
        QRectF sceneRect;
        PathRenderOptions options;
    };

    QTimer timer_;
    QThreadPool pool_;
    Pending pending_;
    std::shared_ptr<std::atomic_bool> cancelFlag_;
    quint64 generation_ = 0;
    bool busy_ = false;
};

}  // namespace ld::core
