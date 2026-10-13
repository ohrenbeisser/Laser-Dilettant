#include "device/GrblStreamer.h"

GrblStreamer::GrblStreamer(int bufferSize)
    : m_bufferSize(bufferSize)
{
}

bool GrblStreamer::needsExclusive(const QString& line)
{
    const QString l = line.trimmed().toUpper();
    if (l.startsWith(QLatin1Char('$')))
        return !l.startsWith(QLatin1String("$J="));
    // Commands that store coordinate data in the EEPROM.
    return l.startsWith(QLatin1String("G10")) || l.startsWith(QLatin1String("G28.1"))
        || l.startsWith(QLatin1String("G30.1"));
}

void GrblStreamer::enqueue(const QString& line, ReplyHandler handler)
{
    Entry e;
    e.line = line;
    e.length = static_cast<int>(line.toUtf8().size()) + 1;
    e.exclusive = needsExclusive(line);
    e.handler = std::move(handler);
    m_pending.push_back(std::move(e));
}

void GrblStreamer::setJob(const QStringList& lines, int passes)
{
    m_jobLines = lines;
    m_jobTotal = static_cast<qint64>(lines.size()) * qMax(1, passes);
    m_jobSent = 0;
    m_jobAcked = 0;
    m_jobHeld = false;
}

void GrblStreamer::clearJob()
{
    m_jobLines.clear();
    m_jobTotal = m_jobSent = m_jobAcked = 0;
    m_jobHeld = false;
}

QString GrblStreamer::jobLine(qint64 index) const
{
    if (m_jobLines.isEmpty() || index < 0)
        return {};
    return m_jobLines.at(static_cast<int>(index % m_jobLines.size()));
}

bool GrblStreamer::nextEntry(Entry* e) const
{
    if (!m_pending.empty()) {
        *e = m_pending.front();
        return true;
    }
    if (m_jobHeld || m_jobSent >= m_jobTotal)
        return false;
    e->line = jobLine(m_jobSent);
    e->length = static_cast<int>(e->line.toUtf8().size()) + 1;
    e->exclusive = needsExclusive(e->line);
    e->job = true;
    e->jobIndex = m_jobSent;
    e->handler = {};
    return true;
}

void GrblStreamer::popNext()
{
    if (!m_pending.empty())
        m_pending.pop_front();
    else
        ++m_jobSent;
}

QList<QByteArray> GrblStreamer::takeSendable(QStringList* commandLines)
{
    QList<QByteArray> out;
    Entry e;
    while (nextEntry(&e)) {
        if (!m_inFlight.empty() && m_inFlight.back().exclusive)
            break;
        if (e.exclusive && !m_inFlight.empty())
            break;
        // A line longer than the whole buffer can never fit; send it alone
        // so the stream does not stall (GRBL will answer with error:11).
        const bool fits = m_bytesInFlight + e.length <= m_bufferSize;
        if (!fits && !m_inFlight.empty())
            break;
        popNext();
        m_bytesInFlight += e.length;
        out.append(e.line.toUtf8() + '\n');
        if (commandLines && !e.job)
            commandLines->append(e.line);
        m_inFlight.push_back(std::move(e));
        e = Entry();
    }
    return out;
}

bool GrblStreamer::acknowledge(Entry* out)
{
    if (m_inFlight.empty())
        return false;
    Entry e = std::move(m_inFlight.front());
    m_inFlight.pop_front();
    m_bytesInFlight -= e.length;
    if (e.job)
        ++m_jobAcked;
    if (out)
        *out = std::move(e);
    return true;
}

void GrblStreamer::abortJob()
{
    for (Entry& e : m_inFlight)
        e.job = false;
    clearJob();
}

void GrblStreamer::clear()
{
    m_pending.clear();
    m_inFlight.clear();
    m_bytesInFlight = 0;
    clearJob();
}

bool GrblStreamer::isIdle() const
{
    return m_pending.empty() && m_inFlight.empty() && m_jobSent >= m_jobTotal;
}
