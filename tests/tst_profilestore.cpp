#include "core/ProfileStore.h"
#include "core/Resources.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

class TestProfileStore : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { ldInitResources(); }

    void builtinsAreCopied()
    {
        QTemporaryDir dir;
        ProfileStore store(dir.path());
        store.load();
        QCOMPARE(store.profiles().size(), 4);
        const DeviceProfile* p = store.find(QStringLiteral("diy8000_4050"));
        QVERIFY(p);
        QVERIFY(p->builtin);
        QCOMPARE(p->workAreaX, 400.0);
        QCOMPARE(p->workAreaY, 500.0);
        QCOMPARE(p->baudRate, 9600);
        QCOMPARE(p->laserMode, QStringLiteral("M4"));
        QCOMPARE(p->origin, Origin::BottomLeft);
        QVERIFY(!p->homing);
        QVERIFY(!p->airAssist);
        QCOMPARE(store.find(QStringLiteral("k40_grbl"))->laserType, LaserType::Co2);
        QCOMPARE(store.profiles().first().id, QStringLiteral("diy8000_4050"));
    }

    void builtinsAreReadOnly()
    {
        QTemporaryDir dir;
        ProfileStore store(dir.path());
        store.load();
        DeviceProfile p = *store.find(QStringLiteral("diy8000_1720"));
        p.workAreaX = 1;
        QVERIFY(!store.save(p));
        QVERIFY(!store.remove(p.id));
    }

    void copyRenameDelete()
    {
        QTemporaryDir dir;
        ProfileStore store(dir.path());
        store.load();
        const QString id = store.copy(QStringLiteral("diy8000_4050"), QStringLiteral("Mein Laser"));
        QCOMPARE(id, QStringLiteral("mein_laser"));
        DeviceProfile p = *store.find(id);
        QVERIFY(!p.builtin);
        QCOMPARE(p.workAreaY, 500.0);
        p.name = QStringLiteral("Mein Laser 2");
        p.workAreaX = 410;
        QVERIFY(store.save(p));

        ProfileStore again(dir.path());
        again.load();
        QCOMPARE(again.find(id)->name, QStringLiteral("Mein Laser 2"));
        QCOMPARE(again.find(id)->workAreaX, 410.0);
        QCOMPARE(again.copy(id, QStringLiteral("Mein Laser")), QStringLiteral("mein_laser_2"));
        QVERIFY(again.remove(id));
        QVERIFY(!again.find(id));
    }

    void jsonRoundTrip()
    {
        DeviceProfile p;
        p.id = QStringLiteral("x");
        p.name = QStringLiteral("X");
        p.origin = Origin::TopRight;
        p.laserType = LaserType::Co2;
        p.laserMode = QStringLiteral("M3");
        p.homing = true;
        p.focusPower = 2.5;
        const DeviceProfile r = DeviceProfile::fromJson(p.toJson());
        QCOMPARE(r.origin, Origin::TopRight);
        QCOMPARE(r.laserType, LaserType::Co2);
        QCOMPARE(r.laserMode, QStringLiteral("M3"));
        QVERIFY(r.homing);
        QCOMPARE(r.focusPower, 2.5);
        QCOMPARE(r.screenSignX(), -1);
        QCOMPARE(r.screenSignY(), -1);
    }

    void sanitize()
    {
        QJsonObject o;
        o[QStringLiteral("laserMode")] = QStringLiteral("M7");
        o[QStringLiteral("maxS")] = -5;
        o[QStringLiteral("jogSpeed")] = 500;
        o[QStringLiteral("maxSpeed")] = 100;
        const DeviceProfile p = DeviceProfile::fromJson(o);
        QCOMPARE(p.laserMode, QStringLiteral("M4"));
        QCOMPARE(p.maxS, 1000.0);
        QCOMPARE(p.jogSpeed, 100.0);
    }
};

QTEST_GUILESS_MAIN(TestProfileStore)
#include "tst_profilestore.moc"
