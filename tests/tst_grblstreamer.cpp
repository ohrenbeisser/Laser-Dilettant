#include "device/GrblStreamer.h"

#include <QTest>

class TestGrblStreamer : public QObject
{
    Q_OBJECT
private slots:
    void respectsBufferSize()
    {
        GrblStreamer s(127);
        // 10 lines of 18 chars + '\n' = 19 bytes each; 6 fit (114 bytes).
        for (int i = 0; i < 10; ++i)
            s.enqueue(QStringLiteral("G1X%1Y10.000F1200").arg(100 + i));
        const auto first = s.takeSendable();
        QCOMPARE(first.size(), 6);
        QCOMPARE(s.bytesInFlight(), 114);
        QVERIFY(s.takeSendable().isEmpty());
        GrblStreamer::Entry e;
        QVERIFY(s.acknowledge(&e));
        QCOMPARE(e.line, QStringLiteral("G1X100Y10.000F1200"));
        QCOMPARE(s.takeSendable().size(), 1);
        QCOMPARE(s.bytesInFlight(), 114);
    }

    void exactlyFull()
    {
        GrblStreamer s(127);
        s.enqueue(QString(126, QLatin1Char('A'))); // 127 bytes with '\n'
        s.enqueue(QStringLiteral("B"));
        QCOMPARE(s.takeSendable().size(), 1);
        QCOMPARE(s.bytesInFlight(), 127);
        QVERIFY(s.acknowledge(nullptr));
        QCOMPARE(s.takeSendable().size(), 1);
    }

    void exclusiveLines()
    {
        GrblStreamer s(127);
        s.enqueue(QStringLiteral("G0X1"));
        s.enqueue(QStringLiteral("$32=1"));
        s.enqueue(QStringLiteral("G0X2"));
        // $32=1 waits until G0X1 is answered
        QCOMPARE(s.takeSendable().size(), 1);
        QVERIFY(s.acknowledge(nullptr));
        auto next = s.takeSendable();
        QCOMPARE(next.size(), 1);
        QCOMPARE(next.first(), QByteArray("$32=1\n"));
        // nothing follows until the setting is answered
        QVERIFY(s.takeSendable().isEmpty());
        QVERIFY(s.acknowledge(nullptr));
        QCOMPARE(s.takeSendable().first(), QByteArray("G0X2\n"));
    }

    void needsExclusive()
    {
        QVERIFY(GrblStreamer::needsExclusive(QStringLiteral("$$")));
        QVERIFY(GrblStreamer::needsExclusive(QStringLiteral("$130=400")));
        QVERIFY(GrblStreamer::needsExclusive(QStringLiteral("G10 L20 P1 X0 Y0")));
        QVERIFY(!GrblStreamer::needsExclusive(QStringLiteral("$J=G91 X1 F100")));
        QVERIFY(!GrblStreamer::needsExclusive(QStringLiteral("G1 X1")));
    }

    void jobWithPasses()
    {
        GrblStreamer s(127);
        s.setJob({ QStringLiteral("G1X1"), QStringLiteral("G1X2") }, 3);
        QCOMPARE(s.jobTotal(), 6);
        const auto sent = s.takeSendable();
        QCOMPARE(sent.size(), 6);
        QCOMPARE(sent[2], QByteArray("G1X1\n"));
        GrblStreamer::Entry e;
        for (int i = 0; i < 6; ++i) {
            QVERIFY(s.acknowledge(&e));
            QVERIFY(e.job);
            QCOMPARE(e.jobIndex, i);
        }
        QVERIFY(s.jobComplete());
        QVERIFY(s.isIdle());
    }

    void commandsBeforeJobLines()
    {
        GrblStreamer s(10);
        s.setJob({ QStringLiteral("G1X1") }, 2);
        s.enqueue(QStringLiteral("M5"));
        const auto sent = s.takeSendable();
        QCOMPARE(sent.first(), QByteArray("M5\n"));
    }

    void heldJob()
    {
        GrblStreamer s(127);
        s.setJob({ QStringLiteral("G1X1"), QStringLiteral("G1X2") }, 1);
        s.setJobHeld(true);
        QVERIFY(s.takeSendable().isEmpty());
        s.setJobHeld(false);
        QCOMPARE(s.takeSendable().size(), 2);
    }

    void abortJobKeepsInFlight()
    {
        GrblStreamer s(127);
        s.setJob({ QStringLiteral("G1X1"), QStringLiteral("G1X2") }, 1);
        QCOMPARE(s.takeSendable().size(), 2);
        s.abortJob();
        QVERIFY(!s.hasJob());
        QCOMPARE(s.linesInFlight(), 2);
        GrblStreamer::Entry e;
        QVERIFY(s.acknowledge(&e));
        QVERIFY(!e.job);
    }

    void tooLongLineIsSentAlone()
    {
        GrblStreamer s(20);
        s.enqueue(QString(30, QLatin1Char('X')));
        QCOMPARE(s.takeSendable().size(), 1);
    }

    void clear()
    {
        GrblStreamer s(127);
        s.enqueue(QStringLiteral("G0X1"));
        s.takeSendable();
        s.enqueue(QStringLiteral("G0X2"));
        s.clear();
        QVERIFY(s.isIdle());
        QCOMPARE(s.bytesInFlight(), 0);
        QVERIFY(!s.acknowledge(nullptr));
    }
};

QTEST_GUILESS_MAIN(TestGrblStreamer)
#include "tst_grblstreamer.moc"
