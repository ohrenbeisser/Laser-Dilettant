// Erzeugt G-Code im Hintergrund (eigener Thread), entweder nur zum Schätzen der Dauer oder als Datei.
// Wie RasterRunner: Eine neue Anfrage bricht die laufende ab; es kommt nur das Ergebnis der letzten.
#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

#include "core/gcode.h"

namespace ld::core {

class GcodeRunner : public QObject {
    Q_OBJECT

public:
    explicit GcodeRunner(int debounceMs = 0, QObject* parent = nullptr);
    ~GcodeRunner() override;

    // Startet die Erzeugung. Bei leerem outputPath wird nur gezählt (Zeitschätzung), sonst wird die
    // Datei atomar geschrieben (erst bei Erfolg ersetzt sie eine vorhandene Datei).
    void request(const GcodeJob& job, const QString& outputPath = QString());
    void cancel();
    bool isBusy() const { return busy_; }

signals:
    void started();
    void progress(double fraction);
    // outputPath ist leer bei reiner Schätzung. Nach cancel() kommt kein Signal.
    void finished(const ld::core::GcodeResult& result, const QString& outputPath);

private:
    void startJob();

    struct Pending {
        GcodeJob job;
        QString outputPath;
    };

    QTimer timer_;
    QThreadPool pool_;
    Pending pending_;
    std::shared_ptr<std::atomic_bool> cancelFlag_;
    quint64 generation_ = 0;
    bool busy_ = false;
};

}  // namespace ld::core
