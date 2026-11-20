#include "core/ncloader.h"

#include <QMetaObject>

namespace ld::core {

NcLoader::NcLoader(QObject* parent) : QObject(parent)
{
    pool_.setMaxThreadCount(1);
}

NcLoader::~NcLoader()
{
    if (cancelFlag_) cancelFlag_->store(true);
    pool_.waitForDone();   // erst danach darf das Objekt verschwinden
}

void NcLoader::cancel()
{
    if (cancelFlag_) cancelFlag_->store(true);
    ++generation_;   // ein noch kommendes Ergebnis wird verworfen
    busy_ = false;
}

void NcLoader::load(const QString& src, const QString& dst, const MachineSetup& machine)
{
    cancel();
    cancelFlag_ = std::make_shared<std::atomic_bool>(false);
    busy_ = true;
    const quint64 generation = ++generation_;
    const auto flag = cancelFlag_;
    pool_.start([this, src, dst, machine, generation, flag]() {
        const NcAnalysis result = sanitizeNcFile(
            src, dst, machine, flag.get(), [this, generation](double fraction) {
                QMetaObject::invokeMethod(
                    this, [this, generation, fraction] { if (generation == generation_) emit progress(fraction); },
                    Qt::QueuedConnection);
            });
        QMetaObject::invokeMethod(
            this,
            [this, generation, result, src, dst] {
                if (generation != generation_) return;   // überholt oder abgebrochen
                busy_ = false;
                emit finished(result, src, dst);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace ld::core
