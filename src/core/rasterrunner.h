// Rechnet processImage() im Hintergrund (eigener Thread), damit die Oberfläche nicht einfriert.
//
// Jede neue Anfrage bricht die laufende ab. Eine kurze Wartezeit (debounceMs) fasst schnelle
// Folgen von Anfragen zusammen, zum Beispiel beim Ziehen eines Reglers. Es kommt nur das
// Ergebnis der jeweils letzten Anfrage an; ältere Ergebnisse werden verworfen.
#pragma once

#include <QImage>
#include <QObject>
#include <QSizeF>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

#include "core/imageprocessing.h"
#include "core/imagesettings.h"
#include "core/svgmodel.h"

namespace ld::core {

class RasterRunner : public QObject {
    Q_OBJECT

public:
    explicit RasterRunner(int debounceMs = 150, QObject* parent = nullptr);
    ~RasterRunner() override;

    // Fordert ein neues Ergebnis an. Das Originalbild wird nicht kopiert (QImage teilt die Daten).
    // Mit svg wird statt des Originalbildes die Füllung der SVG-Datei in Zielgröße gezeichnet
    // (für Full; settings.svg.fullStrokes bestimmt, ob Striche als Fläche dazugehören).
    void request(const QImage& source, const ImageSettings& settings, const QSizeF& sizeMm,
                 std::shared_ptr<const SvgModel> svg = nullptr);

    // Bricht eine laufende oder wartende Anfrage ab; es kommt kein Ergebnis.
    void cancel();

    // Wartet oder rechnet noch eine Anfrage?
    bool isBusy() const { return busy_; }

signals:
    void started();
    void finished(const ld::core::ProcessResult& result);

private:
    void startJob();

    struct Pending {
        QImage source;
        ImageSettings settings;
        QSizeF sizeMm;
        std::shared_ptr<const SvgModel> svg;
    };

    QTimer timer_;
    QThreadPool pool_;
    Pending pending_;
    std::shared_ptr<std::atomic_bool> cancelFlag_;
    quint64 generation_ = 0;
    bool busy_ = false;
};

}  // namespace ld::core
