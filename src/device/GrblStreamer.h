#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>

// Character-counting streaming protocol from the GRBL wiki.
//
// GRBL's serial receive buffer holds 128 bytes. We keep track of every line
// that was sent but not yet answered with "ok" or "error:N"; a new line is
// only sent when it fits into the remaining space. Pure logic without I/O so
// it can be unit-tested; the driver writes what takeSendable() returns.
//
// Lines that make GRBL write to its EEPROM ($N=..., $RST, G10, ...) are sent
// "exclusively": only when nothing else is in flight, and nothing follows
// until they are answered. GRBL may drop incoming characters while writing the
// EEPROM.
class GrblStreamer
{
public:
    using ReplyHandler = std::function<void(bool ok, const QString& response)>;

    struct Entry
    {
        QString line;
        int length = 0;          // bytes including '\n'
        bool exclusive = false;
        bool job = false;
        qint64 jobIndex = -1;    // index within the job (all passes)
        ReplyHandler handler;
    };

    explicit GrblStreamer(int bufferSize = 127);

    void setBufferSize(int bytes) { m_bufferSize = bytes; }
    int bufferSize() const { return m_bufferSize; }

    // Queues a command. Lines must already be free of '\n'.
    void enqueue(const QString& line, ReplyHandler handler = {});

    // Starts streaming a job; lines are taken lazily, repeated `passes` times.
    void setJob(const QStringList& lines, int passes);
    void clearJob();
    bool hasJob() const { return m_jobTotal > 0; }
    qint64 jobTotal() const { return m_jobTotal; }
    qint64 jobSent() const { return m_jobSent; }
    qint64 jobAcknowledged() const { return m_jobAcked; }
    bool jobComplete() const { return hasJob() && m_jobAcked >= m_jobTotal; }
    QString jobLine(qint64 index) const;

    // While held, no further job lines are sent (commands still are).
    void setJobHeld(bool held) { m_jobHeld = held; }
    bool jobHeld() const { return m_jobHeld; }

    // Returns lines (with '\n') that may be written now and marks them as
    // in flight. If commandLines is given, the texts of non-job lines are
    // appended to it (for the console).
    QList<QByteArray> takeSendable(QStringList* commandLines = nullptr);

    // Drops the remaining job lines but keeps the lines in flight, because
    // GRBL will still answer them.
    void abortJob();

    // Removes the oldest in-flight entry after "ok"/"error". Returns false if
    // nothing was in flight (unexpected reply).
    bool acknowledge(Entry* out);

    // Forgets everything, e.g. after a soft reset.
    void clear();

    int bytesInFlight() const { return m_bytesInFlight; }
    int linesInFlight() const { return static_cast<int>(m_inFlight.size()); }
    int pendingCommands() const { return static_cast<int>(m_pending.size()); }
    bool isIdle() const;

    static bool needsExclusive(const QString& line);

private:
    bool nextEntry(Entry* e) const;
    void popNext();

    int m_bufferSize;
    int m_bytesInFlight = 0;
    std::deque<Entry> m_pending;
    std::deque<Entry> m_inFlight;

    QStringList m_jobLines;
    qint64 m_jobTotal = 0;
    qint64 m_jobSent = 0;
    qint64 m_jobAcked = 0;
    bool m_jobHeld = false;
};
