#include "device/SimulatedGrbl.h"

#include <QSignalSpy>
#include <QTest>

// Talks to the simulator directly, byte by byte.
class TestSimulator : public QObject
{
    Q_OBJECT
private:
    struct Rig
    {
        SimulatedGrbl sim;
        QByteArray out;
        Rig()
        {
            sim.setBootDelayMs(10);
            sim.setTimeScale(50);
            QObject::connect(&sim, &SimulatedGrbl::dataReceived, [this](const QByteArray& d) { out += d; });
            sim.open(nullptr);
            QTRY_VERIFY_WITH_TIMEOUT(out.contains("Grbl 1.1h"), 2000);
            out.clear();
        }
        // Sends a line and waits for "ok" or "error".
        QByteArray command(const QByteArray& line)
        {
            out.clear();
            sim.write(line + '\n');
            QElapsedTimer t;
            t.start();
            while (!out.contains("ok\r\n") && !out.contains("error:") && t.elapsed() < 2000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            return out;
        }
        bool waitIdle()
        {
            QElapsedTimer t;
            t.start();
            while (sim.state() != MachineState::Idle && t.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            return sim.state() == MachineState::Idle;
        }
    };

private slots:
    void settings()
    {
        Rig r;
        const QByteArray o = r.command("$$");
        QVERIFY(o.contains("$130=400.000\r\n"));
        QVERIFY(o.contains("$32=1\r\n"));
        QVERIFY(o.endsWith("ok\r\n"));
        QVERIFY(r.command("$110=5000").contains("ok"));
        QCOMPARE(r.sim.settings().raw(110), QStringLiteral("5000.000"));
        QVERIFY(r.command("$999=1").contains("error:3"));
        QVERIFY(r.command("$20=1").contains("error:10"));
        QVERIFY(r.command("$Q").contains("error:3"));
    }

    void info()
    {
        Rig r;
        QVERIFY(r.command("$I").contains("[VER:1.1h"));
        QVERIFY(r.command("$G").contains("[GC:G0 G54 G17 G21 G90 G94 M5 M9"));
        QVERIFY(r.command("$#").contains("[G54:"));
    }

    void gcodeErrors()
    {
        Rig r;
        QVERIFY(r.command("G1 X10").contains("error:22"));
        QVERIFY(r.command("G1 Q5").contains("error:20"));
        QVERIFY(r.command("G0 G1 X5").contains("error:21"));
        QVERIFY(r.command("X").contains("error:2"));
        QVERIFY(r.command("1").contains("error:1"));
        QVERIFY(r.command("G77").contains("error:20"));
        QVERIFY(r.command("M56").contains("error:20"));
        QVERIFY(r.command(QByteArray(90, 'G')).contains("error:11"));
        QVERIFY(r.command("G21 G90 ; comment").contains("ok"));
    }

    void motion()
    {
        Rig r;
        QVERIFY(r.command("G1 X10 Y5 F3000").contains("ok"));
        QVERIFY(r.waitIdle());
        QCOMPARE(r.sim.machinePosition().x, 10.0);
        QCOMPARE(r.sim.machinePosition().y, 5.0);
        QVERIFY(r.command("G91 G0 X-2").contains("ok"));
        QVERIFY(r.waitIdle());
        QCOMPARE(r.sim.machinePosition().x, 8.0);
    }

    void workOffsetAndStatus()
    {
        Rig r;
        QVERIFY(r.command("G0 X20 Y10").contains("ok"));
        QVERIFY(r.waitIdle());
        QVERIFY(r.command("G10 L20 P1 X0 Y0").contains("ok"));
        r.out.clear();
        r.sim.write("?");
        QTRY_VERIFY_WITH_TIMEOUT(r.out.contains(">"), 1000);
        QVERIFY(r.out.contains("<Idle|MPos:20.000,10.000,0.000|FS:0,0|WCO:20.000,10.000,0.000>"));
        r.sim.settings().set(10, QStringLiteral("0"));
        r.out.clear();
        r.sim.write("?");
        QTRY_VERIFY_WITH_TIMEOUT(r.out.contains(">"), 1000);
        QVERIFY(r.out.contains("WPos:0.000,0.000,0.000"));
    }

    void jogAndCancel()
    {
        Rig r;
        r.sim.setTimeScale(1);
        QVERIFY(r.command("$J=G91 G21 X100 F600").contains("ok"));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Jog, 1000);
        r.sim.write(QByteArray(1, char(0x85)));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Idle, 1000);
        QVERIFY(r.sim.machinePosition().x < 100);
        QVERIFY(r.command("$J=G91 X1").contains("error:22"));
        QVERIFY(r.command("$J=G91 X1 M3 F100").contains("error:16"));
    }

    void feedHoldResume()
    {
        Rig r;
        r.sim.setTimeScale(1);
        QVERIFY(r.command("G1 X50 F600").contains("ok"));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Run, 1000);
        r.sim.write("!");
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Hold, 1000);
        r.out.clear();
        QTest::qWait(300);
        r.sim.write("?");
        QTRY_VERIFY_WITH_TIMEOUT(r.out.contains("<Hold:0"), 1000);
        const double x = r.sim.machinePosition().x;
        QTest::qWait(100);
        QCOMPARE(r.sim.machinePosition().x, x);
        r.sim.write("~");
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Run, 1000);
        r.sim.write(QByteArray(1, char(0x18)));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Alarm, 1000); // reset while moving
        QVERIFY(r.out.contains("ALARM:3"));
    }

    void overrides()
    {
        Rig r;
        r.sim.write(QByteArray("\x91\x91\x93", 3));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.feedOverride(), 121, 1000);
        r.sim.write(QByteArray("\x9B\x9D", 2));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.spindleOverride(), 89, 1000);
        r.sim.write(QByteArray("\x90", 1));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.feedOverride(), 100, 1000);
        for (int i = 0; i < 30; ++i)
            r.sim.write(QByteArray("\x92", 1));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.feedOverride(), 10, 1000);
    }

    void rxOverflowIsDetected()
    {
        Rig r;
        r.sim.setTimeScale(0.01); // planner fills, rx buffer is not drained
        QByteArray flood;
        for (int i = 0; i < 40; ++i)
            flood += "G1X" + QByteArray::number(i + 1) + "F100\n";
        r.sim.write(flood);
        QTRY_VERIFY_WITH_TIMEOUT(r.sim.rxOverflowed(), 2000);
    }

    void laserModeFiring()
    {
        Rig r;
        // M3 in laser mode: on at standstill only while G1 is the modal motion
        QVERIFY(r.command("M3 S10").contains("ok"));
        QVERIFY(!r.sim.laserFiring());
        QVERIFY(r.command("G1 F100").contains("ok"));
        QVERIFY(r.sim.laserFiring());
        QVERIFY(r.command("M5").contains("ok"));
        QVERIFY(!r.sim.laserFiring());
        // M4 is dynamic: off without motion
        QVERIFY(r.command("M4 S10").contains("ok"));
        QVERIFY(!r.sim.laserFiring());
        QVERIFY(r.command("M5").contains("ok"));
        // without laser mode M3 is on immediately
        QVERIFY(r.command("$32=0").contains("ok"));
        QVERIFY(r.command("G0 M3 S10").contains("ok"));
        QVERIFY(r.sim.laserFiring());
        QVERIFY(r.command("M5").contains("ok"));
    }

    void homing()
    {
        Rig r;
        QVERIFY(r.command("$H").contains("error:5"));
        QVERIFY(r.command("$22=1").contains("ok"));
        QVERIFY(r.command("G0 X10").contains("ok"));
        QVERIFY(r.waitIdle());
        const QByteArray o = r.command("$H");
        QVERIFY(o.contains("ok"));
        QCOMPARE(r.sim.machinePosition().x, 0.0);
    }

    void alarmLocksGcode()
    {
        Rig r;
        r.sim.settings().set(22, QStringLiteral("1"));
        r.sim.write(QByteArray(1, char(0x18)));
        QTRY_COMPARE_WITH_TIMEOUT(r.sim.state(), MachineState::Alarm, 1000);
        QVERIFY(r.command("G0 X1").contains("error:9"));
        QVERIFY(r.command("$X").contains("[MSG:Caution: Unlocked]"));
        QCOMPARE(r.sim.state(), MachineState::Idle);
    }

    void bootSwallowsInput()
    {
        SimulatedGrbl sim;
        QByteArray out;
        connect(&sim, &SimulatedGrbl::dataReceived, this, [&](const QByteArray& d) { out += d; });
        sim.setBootDelayMs(200);
        sim.open(nullptr);
        sim.write("$I\n");
        QTRY_VERIFY_WITH_TIMEOUT(out.contains("Grbl"), 2000);
        QTest::qWait(100);
        QVERIFY(!out.contains("VER"));
    }

    void baudRateDelay()
    {
        SimulatedGrbl sim;
        QByteArray out;
        connect(&sim, &SimulatedGrbl::dataReceived, this, [&](const QByteArray& d) { out += d; });
        sim.setBootDelayMs(0);
        sim.setBaudRate(9600);
        sim.open(nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(out.contains("help]"), 2000);
        out.clear();
        QElapsedTimer t;
        t.start();
        sim.write("$$\n"); // ~430 bytes answer at 960 bytes/s
        QTRY_VERIFY_WITH_TIMEOUT(out.contains("ok"), 3000);
        QVERIFY2(t.elapsed() > 300, qPrintable(QString::number(t.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestSimulator)
#include "tst_simulator.moc"
