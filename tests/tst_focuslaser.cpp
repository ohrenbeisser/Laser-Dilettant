#include "SimHelpers.h"
#include "device/FocusLaser.h"

#include <QSignalSpy>
#include <QTest>

class TestFocusLaser : public QObject
{
    Q_OBJECT
private slots:
    void onAndOff()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
        FocusLaser focus(&rig.driver);
        QString error;
        QVERIFY2(focus.turnOn(1.0, 1000, &error), qPrintable(error));
        QVERIFY(focus.isOn());
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        QCOMPARE(rig.sim->spindleValue(), 10.0);
        QVERIFY(rig.sim->receivedLines().contains(QStringLiteral("M3S10")));
        focus.turnOff();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.sim->laserFiring(), 2000);
        QCOMPARE(rig.sim->settings().number(32), 1.0);
    }

    void autoOff()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
        FocusLaser focus(&rig.driver);
        focus.setAutoOffSeconds(1);
        QVERIFY(focus.turnOn(1.0, 1000));
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!focus.isOn(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.sim->laserFiring(), 2000);
    }

    void temporarilyDisablesLaserMode()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
        FocusLaser focus(&rig.driver);
        focus.setDisableLaserMode(true);
        QSignalSpy pending(&focus, &FocusLaser::pendingRestoreChanged);
        QVERIFY(focus.turnOn(1.0, 1000));
        QCOMPARE(focus.pendingRestore(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->settings().number(32), 0.0, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        focus.turnOff();
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->settings().number(32), 1.0, 2000);
        QTRY_COMPARE(focus.pendingRestore(), -1);
        QCOMPARE(pending.count(), 2);
    }

    void restoredAfterEmergencyStop()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
        FocusLaser focus(&rig.driver);
        focus.setDisableLaserMode(true);
        QVERIFY(focus.turnOn(2.0, 1000));
        QTRY_VERIFY_WITH_TIMEOUT(rig.sim->laserFiring(), 2000);
        rig.driver.emergencyStop();
        QVERIFY(!focus.isOn());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.sim->laserFiring(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->settings().number(32), 1.0, 2000);
        QTRY_COMPARE(focus.pendingRestore(), -1);
    }

    void restoredOnDisconnect()
    {
        SimRig rig;
        QVERIFY(rig.connect());
        QTRY_COMPARE(rig.driver.status().state, MachineState::Idle);
        FocusLaser focus(&rig.driver);
        focus.setDisableLaserMode(true);
        QVERIFY(focus.turnOn(1.0, 1000));
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->settings().number(32), 0.0, 2000);
        QPointer<SimulatedGrbl> sim = rig.sim;
        double laserMode = -1;
        bool firing = true;
        connect(&rig.driver, &GrblDriver::disconnected, this, [&] {
            laserMode = sim->settings().number(32);
            firing = sim->laserFiring();
        });
        QSignalSpy disc(&rig.driver, &GrblDriver::disconnected);
        rig.driver.disconnectDevice();
        QTRY_COMPARE_WITH_TIMEOUT(disc.count(), 1, 3000);
        QCOMPARE(laserMode, 1.0);
        QVERIFY(!firing);
    }

    void restoredOnNextConnection()
    {
        // Simulates a crash: $32 is 0 on the device and the restore is pending.
        SimRig rig;
        rig.configure = [](SimulatedGrbl* s) { s->settings().set(32, QStringLiteral("0")); };
        FocusLaser focus(&rig.driver);
        focus.setPendingRestore(1);
        QVERIFY(rig.connect());
        QTRY_COMPARE_WITH_TIMEOUT(rig.sim->settings().number(32), 1.0, 2000);
        QTRY_COMPARE(focus.pendingRestore(), -1);
    }

    void refusedWhenNotIdle()
    {
        SimRig rig;
        FocusLaser focus(&rig.driver);
        QString error;
        QVERIFY(!focus.turnOn(1.0, 1000, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFocusLaser)
#include "tst_focuslaser.moc"
