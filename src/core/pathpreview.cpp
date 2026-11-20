#include "core/pathpreview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>

namespace ld::core {

PathPreview::PathPreview(QObject* parent) : QObject(parent), generator_(300)
{
    loadPool_.setMaxThreadCount(1);
    connect(&generator_, &GcodeRunner::finished, this,
            [this](const GcodeResult& result, const QString& path) { onGenerated(result, path); });
}

PathPreview::~PathPreview()
{
    if (loadCancel_) {
        loadCancel_->store(true);
    }
    generator_.cancel();
    loadPool_.waitForDone();
}

void PathPreview::setState(State state, const QString& error)
{
    state_ = state;
    error_ = error;
    emit stateChanged();
}

void PathPreview::discard()
{
    external_ = false;
    ++generation_;
    generator_.cancel();
    if (loadCancel_) {
        loadCancel_->store(true);
    }
    if (!filePath_.isEmpty()) {
        QFile::remove(filePath_);
        filePath_.clear();
    }
    data_.reset();
    stats_ = {};
}

void PathPreview::setJob(const GcodeJob& job)
{
    discard();
    job_ = job;
    setState(State::Stale);
}

void PathPreview::clearJob()
{
    discard();
    job_ = GcodeJob{};
    if (state_ != State::Idle) {
        setState(State::Idle);
    }
}

void PathPreview::request()
{
    if (state_ != State::Stale) {
        return;   // schon fertig, in Arbeit oder kein Auftrag
    }
    if (!tempDir_.isValid()) {
        setState(State::Failed, QCoreApplication::translate("ld::core", "Der temporäre Ordner kann nicht angelegt werden."));
        return;
    }
    filePath_ = tempDir_.filePath(QStringLiteral("vorschau-%1.nc").arg(++fileCounter_));
    setState(State::Generating);
    generator_.request(job_, filePath_);
}

void PathPreview::onGenerated(const GcodeResult& result, const QString& path)
{
    if (state_ != State::Generating || path != filePath_) {
        return;
    }
    if (!result.ok) {
        filePath_.clear();
        setState(State::Failed, result.error);
        return;
    }
    stats_ = result.stats;
    startLoading(path);
}

void PathPreview::setExternalFile(const QString& file, const GcodeJob& job, const GcodeStats& stats)
{
    discard();
    job_ = job;
    external_ = true;
    filePath_ = file;
    stats_ = stats;
    startLoading(file);
}

void PathPreview::startLoading(const QString& path)
{
    setState(State::Loading);

    // Wege im Hintergrund lesen.
    loadCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto flag = loadCancel_;
    const quint64 generation = generation_;
    const GcodeJob job = job_;
    loadPool_.start([this, generation, flag, job, path]() {
        std::shared_ptr<PathData> loaded = PathData::load(path, job, flag.get());
        QMetaObject::invokeMethod(
            this,
            [this, generation, loaded]() {
                if (generation != generation_ || state_ != State::Loading) {
                    return;   // überholt
                }
                if (!loaded) {
                    setState(State::Failed, QCoreApplication::translate("ld::core", "Die Laserwege konnten nicht gelesen werden."));
                    return;
                }
                data_ = loaded;
                if (external_) {
                    // Bei einer fertigen Datei kommt die Dauer aus dem Lesen der Wege.
                    stats_.seconds = loaded->totalSeconds;
                    stats_.burnLengthMm = loaded->burnLengthMm;
                    stats_.travelLengthMm = loaded->rapidLengthMm;
                }
                setState(State::Ready);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace ld::core
