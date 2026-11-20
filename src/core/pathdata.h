// Die Laserwege eines G-Codes, aufbereitet für Anzeige und Simulation (Meilenstein M5,
// siehe docs/planung.md 3.11).
//
// Die Wege werden aus der (temporären) G-Code-Datei gelesen und in das Koordinatensystem der
// Arbeitsfläche umgerechnet: Millimeter aus Sicht des Bedieners, Ursprung in der MITTE des
// Auftrags, Y nach unten (wie die Szene der Arbeitsfläche). Jedes Wegstück kennt Start- und
// Endzeit, die Zeitachse ist dieselbe Rechnung wie bei der Dauer-Schätzung.
#pragma once

#include <QPointF>
#include <QRectF>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "core/gcode.h"

namespace ld::core {

enum class SegmentKind : unsigned char {
    Rapid = 0,   // G0
    Travel = 1,  // G1 ohne Leistung (Überscan, Lücken)
    Burn = 2,    // G1 mit Leistung
};

struct PathSegment {
    float x0, y0, x1, y1;   // mm, Szene der Arbeitsfläche (Mitte des Auftrags = 0)
    SegmentKind kind;
};

// Maschinenpunkt in die Szene der Arbeitsfläche umrechnen (Umkehrung der Rechnung im Erzeuger).
// offsetMm: Lage der Grafik zum Nullpunkt (ObjectTransform::offsetXMm/offsetYMm).
QPointF machineToScene(const MachineSetup& setup, JobOrigin origin, const QSizeF& boundsMm,
                       const QPointF& machine, const QPointF& offsetMm = QPointF());

class PathData;

// Ordnet eine gemessene Laserposition (Szene, mm) einer Stelle im Weg zu: Sucht ab Wegstück `from`
// höchstens `window` Wegstücke vorwärts nach dem, das der Position am nächsten liegt (bei Gleichstand das
// frühere). So läuft die Zuordnung während des Auftrags nur vorwärts. Liefert den Index dieses Wegstücks;
// alle davor gelten als erledigt. distanceOut (optional) bekommt den Abstand in mm.
size_t locateOnPath(const PathData& data, size_t from, const QPointF& position, size_t window = 4000,
                    double* distanceOut = nullptr);

class PathData {
public:
    std::vector<PathSegment> segments;
    std::vector<float> startTimes;   // Sekunden seit Beginn, je Wegstück
    std::vector<float> endTimes;
    QRectF bounds;                   // Umschließendes Rechteck aller Wegstücke
    double totalSeconds = 0.0;       // Dauer des ganzen Auftrags (auch der ausgelassenen Zeilen)
    double burnLengthMm = 0.0;
    double travelLengthMm = 0.0;
    double rapidLengthMm = 0.0;
    long long fileMoves = 0;         // Fahrten in der Datei
    bool coarse = false;             // Bei sehr großen Aufträgen wurde nur jede k-te Zeile übernommen
    int lineStride = 1;              // k

    // Liest die Datei. Mehr als maxSegments Wegstücke werden nicht gespeichert: Dann wird nur jede
    // k-te Zeile übernommen (coarse). Gibt nullptr bei Abbruch oder Lesefehler zurück.
    static std::shared_ptr<PathData> load(const QString& file, const GcodeJob& job,
                                          const std::atomic_bool* cancel = nullptr,
                                          const std::function<void(double)>& progress = {},
                                          size_t maxSegments = 8'000'000);

    // Erstes Wegstück, das zum Zeitpunkt t (Sekunden) noch nicht fertig ist = Zahl der fertigen Stücke.
    size_t indexAt(double seconds) const;
    // Position des Kopfes zum Zeitpunkt t (Szene).
    QPointF positionAt(double seconds) const;
};

}  // namespace ld::core
