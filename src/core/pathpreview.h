// Verwaltet die temporäre G-Code-Datei für die Vorschau und liest daraus die Laserwege.
//
// Entscheidungen (planung.md 3.11): Die Datei entsteht nur bei Bedarf (request()), bleibt bis zur
// nächsten Änderung bestehen (setJob() verwirft sie) und liegt in einem temporären Ordner, der
// beim Beenden gelöscht wird.
#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "core/gcode.h"
#include "core/gcoderunner.h"
#include "core/pathdata.h"

namespace ld::core {

class PathPreview : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,        // kein Auftrag
        Stale,       // Auftrag da, Datei noch nicht erzeugt
        Generating,  // G-Code wird geschrieben
        Loading,     // Wege werden gelesen
        Ready,       // Datei und Wege liegen vor
        Failed,      // Fehler, siehe error()
    };

    explicit PathPreview(QObject* parent = nullptr);
    ~PathPreview() override;

    // Neuer Auftrag: verwirft Datei und Wege des alten. Es wird noch nichts erzeugt.
    void setJob(const GcodeJob& job);
    // Kein Auftrag mehr (zum Beispiel, solange das Raster noch berechnet wird).
    void clearJob();
    // Erzeugt Datei und Wege, falls noch nicht geschehen.
    void request();
    // NC-Auftrag (planung.md 3.21): Die G-Code-Datei liegt schon vor (eine bereinigte Kopie, die der Vorschau gehört und
    // beim Verwerfen gelöscht wird). Die Wege werden daraus gelesen; job liefert Maschinenprofil und die Zuordnung der
    // Koordinaten (Größe 0: Nullpunkt der Anzeige = Nullpunkt der Datei). stats: Zeilen und Größe der Datei.
    void setExternalFile(const QString& file, const GcodeJob& job, const GcodeStats& stats);
    bool isExternal() const { return external_; }

    State state() const { return state_; }
    QString error() const { return error_; }
    const GcodeJob& job() const { return job_; }
    QString filePath() const { return state_ == State::Ready ? filePath_ : QString(); }
    const GcodeStats& stats() const { return stats_; }
    std::shared_ptr<const PathData> data() const { return data_; }

signals:
    void stateChanged();

private:
    void setState(State state, const QString& error = QString());
    void discard();
    void onGenerated(const GcodeResult& result, const QString& path);
    void startLoading(const QString& path);   // Wege im Hintergrund aus der Datei lesen

    GcodeJob job_;
    State state_ = State::Idle;
    QString error_;
    QString filePath_;
    GcodeStats stats_;
    std::shared_ptr<const PathData> data_;

    QTemporaryDir tempDir_;
    GcodeRunner generator_;
    QThreadPool loadPool_;
    std::shared_ptr<std::atomic_bool> loadCancel_;
    quint64 generation_ = 0;
    int fileCounter_ = 0;
    bool external_ = false;   // die Datei kommt von außen (NC-Auftrag)
};

}  // namespace ld::core
