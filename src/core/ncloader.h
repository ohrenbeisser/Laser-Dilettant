// Lädt eine NC-Datei im Hintergrund: prüfen und bereinigen (siehe ncfile.h). Meldet Fortschritt und das Ergebnis im
// Hauptthread. Ein neuer Aufruf oder cancel() bricht den laufenden ab.
#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "core/machinesetup.h"
#include "core/ncfile.h"

namespace ld::core {

class NcLoader : public QObject {
    Q_OBJECT

public:
    explicit NcLoader(QObject* parent = nullptr);
    ~NcLoader() override;

    // Prüft src und schreibt die bereinigte Kopie nach dst.
    void load(const QString& src, const QString& dst, const MachineSetup& machine);
    void cancel();
    bool isBusy() const { return busy_; }

signals:
    void progress(double fraction);
    // result.ok = false bei Fehler oder Abbruch (result.cancelled).
    void finished(const ld::core::NcAnalysis& result, const QString& src, const QString& dst);

private:
    QThreadPool pool_;
    std::shared_ptr<std::atomic_bool> cancelFlag_;
    quint64 generation_ = 0;
    bool busy_ = false;
};

}  // namespace ld::core
