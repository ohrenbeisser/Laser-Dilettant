#include "SimHelpers.h"

#include <QSignalSpy>
#include <QTest>

namespace {
// A job of `count` moves alternating between two points far enough apart
// that GRBL's planner fills up and the receive buffer is actually used.
GcodeProgram zigzag(int count, double length = 40.0)
{
    GcodeProgram p;
    p.lines << QStringLiteral("G21G90") << QStringLiteral("M4S0");
    for (int i = 0; i < count; ++i)
        p.lines << QStringLiteral("G1X%1Y%2F6000S100").arg(i % 2 ? length : 0.0).arg(i * 0.1, 0, 'f', 1);
    p.lines << QStringLiteral("M5");
    return p;
}
} // namespace

class TestGrblDriver : public QObject
{
    Q_OBJECT
private slots:
    void connectReadsEverything()
    {
        SimRig rig;
        QSignalSpy connectedSpy(&rig.driver, &GrblDriver::connected);
        QVERIFY(rig.connect());
        QCOMPARE(connectedSpy.count(), 1);
        QCOMPARE(rig.driver.firmwareVersion(), QStringLiteral("1.1h"));
        QCOMPARE(rig.driver.settings().number(130), 400.0);
        QCOMPARE(rig.driver.settings().number(32), 1.0);
        QVERIFY(rig.driver.buildInfo().join(QLatin1Char(' ')).contains(QStringLiteral("VER:1.1h")));
        QVERIFY(rig.driver.parserState().contains(QStringLiteral("G90")));
        QVERIFY(rig.sim->receivedLines().contains(QStringLiteral("M5")));
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
    }

    void vendorBanner()
    {
        SimRig rig;
        rig.configure = [](SimulatedGrbl* s) { s->setBanner(QStringLiteral("Grbl 1.1z ['$' for help]")); };
        QVERIFY(rig.connect());
        QCOMPARE(rig.driver.firmwareVersion(), QStringLiteral("1.1z"));
    }

    void bannerAfterSoftReset()
    {
        // Board does not reset when the port opens: the driver sends 0x18.
        SimRig rig;
        rig.driver.setBannerTimeouts(200, 1000);
        rig.configure = [](SimulatedGrbl* s) { s->setResetOnOpen(false); };
        QVERIFY(rig.connect());
        QCOMPARE(rig.sim->resetCount(), 1);
    }

    void noDevice()
    {
        SimRig rig;
        rig.driver.setBannerTimeouts(150, 150);
        rig.configure = [](SimulatedGrbl* s) {
            s->setResetOnOpen(false);
            s->setHung(true);
        };
        QSignalSpy failed(&rig.driver, &GrblDriver::connectFailed);
        ConnectionParams p;
        p.simulator = true;
        rig.driver.connectDevice(p);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("baud")));
        QCOMPARE(rig.driver.status().state, MachineState::Disconnected);
    }

    void streaming_data()
    {
        QTest::addColumn<int>("baud");
        QTest::addColumn<int>("moves");
        QTest::addColumn<double>("length");
        QTest::addColumn<double>("timeScale");
        QTest::newRow("9600") << 9600 << 30 << 150.0 << 10.0;
        QTest::newRow("115200") << 115200 << 120 << 40.0 << 20.0;
        QTest::newRow("unthrottled") << 0 << 120 << 40.0 << 20.0;
    }
    void streaming()
    {
        QFETCH(int, baud);
        QFETCH(int, moves);
        QFETCH(double, length);
        QFETCH(double, timeScale);
        SimRig rig;
        rig.baud = baud;
        rig.timeScale = timeScale;
        QVERIFY(rig.connect());
        const GcodeProgram prog = zigzag(moves, length);
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        QSignalSpy progress(&rig.driver, &GrblDriver::progressChanged);
        rig.driver.startJob(prog, 2);
        QVERIFY(rig.driver.isJobRunning());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        QCOMPARE(finished.first().first().toBool(), true);
        QVERIFY(!rig.driver.isJobRunning());
        QVERIFY(!rig.sim->rxOverflowed());
        QVERIFY(rig.sim->maxRxUsage() <= 127);
        // character counting keeps several lines in GRBL's buffer
        QVERIFY2(rig.sim->maxRxUsage() > 60, qPrintable(QString::number(rig.sim->maxRxUsage())));
        QCOMPARE(rig.sim->executedGcodeLines(), prog.lines.size() * 2 + 1 /* M5 on connect */);
        const auto last = progress.last();
        QCOMPARE(last.at(0).toLongLong(), qint64(prog.lines.size()) * 2);
        QCOMPARE(last.at(1).toLongLong(), qint64(prog.lines.size()) * 2);
    }

    void errorDuringJob()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        // line 1 on the simulator is the M5 sent while connecting
        rig.sim->injectError(1 + 6, 20);
        QSignalSpy paused(&rig.driver, &GrblDriver::jobErrorPaused);
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        rig.driver.startJob(zigzag(30), 1);
        QTRY_COMPARE_WITH_TIMEOUT(paused.count(), 1, 5000);
        QCOMPARE(paused.first().at(0).toLongLong(), 6);
        QCOMPARE(paused.first().at(2).toInt(), 20);
        QVERIFY(rig.driver.streamer().jobHeld());
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Hold, 2000);
        QCOMPARE(finished.count(), 0);
        rig.driver.resume();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(finished.first().first().toBool(), true);
    }

    void pauseAndResume()
    {
        SimRig rig;
        rig.timeScale = 2;
        QVERIFY(rig.connect());
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        rig.driver.startJob(zigzag(20), 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Run, 2000);
        rig.driver.pause();
        QTRY_VERIFY_WITH_TIMEOUT(rig.driver.status().state == MachineState::Hold && rig.driver.status().subState == 0, 3000);
        const double x = rig.sim->machinePosition().x;
        QTest::qWait(200);
        QCOMPARE(rig.sim->machinePosition().x, x);
        rig.driver.resume();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        QVERIFY(finished.first().first().toBool());
    }

    void controlledStop()
    {
        SimRig rig;
        rig.timeScale = 1;
        QVERIFY(rig.connect());
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        QSignalSpy alarms(&rig.driver, &GrblDriver::alarmRaised);
        rig.driver.startJob(zigzag(20), 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Run, 2000);
        rig.driver.stop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(finished.first().first().toBool(), false);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Idle, 3000);
        QCOMPARE(alarms.count(), 0); // position kept: no "abort during cycle"
        QCOMPARE(rig.sim->resetCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->receivedLines().last(), QStringLiteral("M5"), 2000);
        QVERIFY(!rig.sim->laserFiring());
    }

    void emergencyStop()
    {
        SimRig rig;
        rig.timeScale = 1;
        QVERIFY(rig.connect());
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        QSignalSpy estop(&rig.driver, &GrblDriver::emergencyStopped);
        QSignalSpy alarms(&rig.driver, &GrblDriver::alarmRaised);
        rig.driver.startJob(zigzag(20), 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Run, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        rig.driver.emergencyStop();
        QCOMPARE(estop.count(), 1);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!rig.driver.isJobRunning());
        QTRY_COMPARE_WITH_TIMEOUT(alarms.count(), 1, 2000);
        QCOMPARE(alarms.first().first().toInt(), 3);
        QVERIFY(!rig.sim->laserFiring());
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Alarm, 2000);
        // unlock works afterwards
        rig.driver.unlock();
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Idle, 2000);
    }

    void emergencyStopWithoutJob()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        rig.driver.sendCommand(QStringLiteral("M3 S10"));
        rig.driver.sendCommand(QStringLiteral("G1 F100"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        QSignalSpy reset(&rig.driver, &GrblDriver::resetDetected);
        rig.driver.emergencyStop();
        QTRY_COMPARE_WITH_TIMEOUT(reset.count(), 1, 2000);
        QVERIFY(!rig.sim->laserFiring());
    }

    void watchdog()
    {
        SimRig rig;
        rig.driver.setWatchdogTimeout(400);
        QVERIFY(rig.connect());
        QSignalSpy lost(&rig.driver, &GrblDriver::connectionLost);
        QSignalSpy disc(&rig.driver, &GrblDriver::disconnected);
        rig.sim->setHung(true);
        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        QCOMPARE(disc.count(), 1);
        QCOMPARE(rig.driver.status().state, MachineState::Disconnected);
    }

    void cableLossDuringJob()
    {
        SimRig rig;
        rig.timeScale = 1;
        QVERIFY(rig.connect());
        QSignalSpy lost(&rig.driver, &GrblDriver::connectionLost);
        QSignalSpy finished(&rig.driver, &GrblDriver::jobFinished);
        rig.driver.startJob(zigzag(20), 1);
        QTest::qWait(100);
        rig.sim->simulateCableLoss();
        QCOMPARE(lost.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toBool(), false);
    }

    void jogAndZero()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        rig.driver.jog(10, 5, 3000);
        rig.driver.jog(-2.5, 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->state() == MachineState::Idle && rig.sim->machinePosition().x == 7.5, 3000);
        QVERIFY(rig.sim->receivedLines().contains(QStringLiteral("$J=G91G21X10Y5F3000")));
        rig.driver.sendCommand(QStringLiteral("G10 L20 P1 X0 Y0"));
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(rig.driver.status().wpos.x) < 1e-9 && rig.driver.status().mpos.x == 7.5, 2000);
        rig.driver.sendCommand(QStringLiteral("G10 L2 P1 X0 Y0"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.driver.status().wpos.x == 7.5, 2000);
    }

    void jogCancel()
    {
        SimRig rig;
        rig.timeScale = 1;
        QVERIFY(rig.connect());
        rig.driver.jog(200, 0, 600);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Jog, 2000);
        rig.driver.jogCancel();
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Idle, 2000);
        QVERIFY(rig.sim->machinePosition().x < 200);
    }

    void overrides()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        rig.driver.setFeedOverride(135);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->feedOverride(), 135, 2000);
        rig.driver.setFeedOverride(80);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->feedOverride(), 80, 2000);
        rig.driver.setFeedOverride(100);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->feedOverride(), 100, 2000);
        rig.driver.setSpindleOverride(57);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->spindleOverride(), 57, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().ovSpindle, 57, 2000);
        rig.driver.setSpindleOverride(500);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->spindleOverride(), 200, 2000);
    }

    void homingAlarmOnConnect()
    {
        SimRig rig;
        rig.configure = [](SimulatedGrbl* s) { s->settings().set(22, QStringLiteral("1")); };
        QVERIFY(rig.connect());
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Alarm, 2000);
        rig.driver.home();
        QTRY_COMPARE_WITH_TIMEOUT(rig.driver.status().state, MachineState::Idle, 3000);
    }

    void writeSettings()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        bool ok = false;
        bool done = false;
        rig.driver.sendCommand(QStringLiteral("$110=5000"), [&](bool r, const QString&) {
            ok = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(ok);
        QCOMPARE(rig.driver.settings().number(110), 5000.0);
        QCOMPARE(rig.sim->settings().number(110), 5000.0);

        QSignalSpy errors(&rig.driver, &GrblDriver::errorOccurred);
        rig.driver.sendCommand(QStringLiteral("$999=1"));
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("error:3")));

        done = false;
        rig.driver.requestSettings([&](bool, const QString&) { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(rig.driver.settings().raw(110), QStringLiteral("5000.000"));
    }

    void disconnectSendsM5()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QPointer<SimulatedGrbl> sim = rig.sim;
        QSignalSpy disc(&rig.driver, &GrblDriver::disconnected);
        rig.driver.disconnectDevice();
        QTRY_COMPARE_WITH_TIMEOUT(disc.count(), 1, 3000);
        QCOMPARE(rig.driver.status().state, MachineState::Disconnected);
    }
};

QTEST_GUILESS_MAIN(TestGrblDriver)
#include "tst_grbldriver.moc"
