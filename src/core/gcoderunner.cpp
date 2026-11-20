#include "core/gcoderunner.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSaveFile>

namespace ld::core {

GcodeRunner::GcodeRunner(int debounceMs, QObject* parent) : QObject(parent)
{
    pool_.setMaxThreadCount(1);
    timer_.setSingleShot(true);
    timer_.setInterval(debounceMs);
    connect(&timer_, &QTimer::timeout, this, &GcodeRunner::startJob);
}

GcodeRunner::~GcodeRunner()
{
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pool_.waitForDone();  // erst danach darf das Objekt verschwinden
}

void GcodeRunner::request(const GcodeJob& job, const QString& outputPath)
{
    ++generation_;
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    pending_ = {job, outputPath};
    busy_ = true;
    emit started();
    timer_.start();
}

void GcodeRunner::cancel()
{
    ++generation_;
    timer_.stop();
    if (cancelFlag_) {
        cancelFlag_->store(true);
    }
    busy_ = false;
}

void GcodeRunner::startJob()
{
    cancelFlag_ = std::make_shared<std::atomic_bool>(false);
    const quint64 generation = generation_;
    const auto flag = cancelFlag_;
    const Pending job = pending_;

    pool_.start([this, generation, flag, job]() {
        // Läuft im Hintergrundthread.
        auto result = std::make_shared<GcodeResult>();
        double lastPosted = -1.0;
        auto reportProgress = [&](double p) {
            if (p - lastPosted < 0.01 && p < 1.0) {
                return;   // nicht öfter als nötig an die Oberfläche melden
            }
            lastPosted = p;
            QMetaObject::invokeMethod(
                this, [this, generation, p]() { if (generation == generation_) emit progress(p); },
                Qt::QueuedConnection);
        };

        if (job.outputPath.isEmpty()) {
            *result = generateGcode(job.job, {}, flag.get(), reportProgress);
        } else {
            QSaveFile file(job.outputPath);
            if (!file.open(QIODevice::WriteOnly)) {
                result->error = QCoreApplication::translate("ld::core", "Die Datei kann nicht geschrieben werden: %1")
                                    .arg(file.errorString());
            } else {
                *result = generateGcode(
                    job.job,
                    [&file](const QByteArray& chunk) { return file.write(chunk) == chunk.size(); },
                    flag.get(), reportProgress);
                if (result->ok && !file.commit()) {
                    result->ok = false;
                    result->error = QCoreApplication::translate("ld::core", "Die Datei kann nicht geschrieben werden: %1")
                                        .arg(file.errorString());
                } else if (!result->ok) {
                    file.cancelWriting();   // unvollständige Datei verwerfen, alte bleibt erhalten
                }
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, generation, result, path = job.outputPath]() {
                if (generation != generation_) {
                    return;   // überholt oder abgebrochen
                }
                busy_ = false;
                if (!result->cancelled) {
                    emit finished(*result, path);
                }
            },
            Qt::QueuedConnection);
    });
}

}  // namespace ld::core
