#include "device/GrblStatusParser.h"

#include <QTest>

class TestGrblStatusParser : public QObject
{
    Q_OBJECT
private slots:
    void states_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("state");
        QTest::addColumn<int>("sub");
        QTest::newRow("idle") << "<Idle|MPos:0.000,0.000,0.000|FS:0,0>" << int(MachineState::Idle) << -1;
        QTest::newRow("run") << "<Run|MPos:1,2,3|FS:500,0>" << int(MachineState::Run) << -1;
        QTest::newRow("hold0") << "<Hold:0|MPos:1,2,3|FS:0,0>" << int(MachineState::Hold) << 0;
        QTest::newRow("hold1") << "<Hold:1|MPos:1,2,3|FS:0,0>" << int(MachineState::Hold) << 1;
        QTest::newRow("jog") << "<Jog|MPos:1,2,3|FS:0,0>" << int(MachineState::Jog) << -1;
        QTest::newRow("alarm") << "<Alarm|MPos:1,2,3|FS:0,0>" << int(MachineState::Alarm) << -1;
        QTest::newRow("door") << "<Door:2|MPos:1,2,3|FS:0,0>" << int(MachineState::Door) << 2;
        QTest::newRow("check") << "<Check|MPos:1,2,3|FS:0,0>" << int(MachineState::Check) << -1;
        QTest::newRow("home") << "<Home|MPos:1,2,3|FS:0,0>" << int(MachineState::Home) << -1;
        QTest::newRow("sleep") << "<Sleep|MPos:1,2,3|FS:0,0>" << int(MachineState::Sleep) << -1;
        QTest::newRow("unknown") << "<Weird|MPos:1,2,3>" << int(MachineState::Unknown) << -1;
        QTest::newRow("grbl 0.9") << "<Idle,MPos:0.000,0.000,0.000,WPos:0.000,0.000,0.000>" << int(MachineState::Idle) << -1;
    }
    void states()
    {
        QFETCH(QString, line);
        QFETCH(int, state);
        QFETCH(int, sub);
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(line, &r));
        QCOMPARE(int(r.state), state);
        QCOMPARE(r.subState, sub);
    }

    void notAStatus()
    {
        StatusReport r;
        QVERIFY(!GrblStatusParser::parse(QStringLiteral("ok"), &r));
        QVERIFY(!GrblStatusParser::parse(QStringLiteral("<Idle"), &r));
        QVERIFY(!GrblStatusParser::parse(QStringLiteral("<>"), &r));
    }

    void fields()
    {
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(
            QStringLiteral("<Run|MPos:10.500,-2.250,0.000|Bf:15,128|FS:1200,350|Pn:XY|Ov:110,100,90|WCO:1.000,2.000,0.000|Ln:42|A:S|Foo:bar>"),
            &r));
        QVERIFY(r.mpos);
        QCOMPARE(r.mpos->x, 10.5);
        QCOMPARE(r.mpos->y, -2.25);
        QVERIFY(!r.wpos);
        QCOMPARE(*r.feed, 1200.0);
        QCOMPARE(*r.spindle, 350.0);
        QCOMPARE(*r.ovFeed, 110);
        QCOMPARE(*r.ovRapid, 100);
        QCOMPARE(*r.ovSpindle, 90);
        QCOMPARE(r.wco->x, 1.0);
        QCOMPARE(*r.plannerFree, 15);
        QCOMPARE(*r.rxFree, 128);
        QCOMPARE(*r.lineNumber, 42);
        QCOMPARE(r.pins, QStringLiteral("XY"));
        QCOMPARE(r.accessories, QStringLiteral("S"));
    }

    void feedOnly()
    {
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Idle|WPos:1,2,3|F:500>"), &r));
        QCOMPARE(*r.feed, 500.0);
        QVERIFY(!r.spindle);
    }

    void mergeMposWithWco()
    {
        MachineStatus s;
        StatusReport r;
        // first report carries WCO
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Idle|MPos:10.000,20.000,0.000|FS:0,0|WCO:5.000,5.000,0.000>"), &r));
        GrblStatusParser::merge(r, &s);
        QCOMPARE(s.wpos.x, 5.0);
        QCOMPARE(s.wpos.y, 15.0);
        // later report without WCO uses the last known offset
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Run|MPos:12.000,20.000,0.000|FS:100,0>"), &r));
        GrblStatusParser::merge(r, &s);
        QCOMPARE(s.wpos.x, 7.0);
        QCOMPARE(s.mpos.x, 12.0);
        QCOMPARE(s.state, MachineState::Run);
    }

    void mergeWpos()
    {
        MachineStatus s;
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Idle|WPos:1.000,2.000,0.000|FS:0,0|WCO:10.000,10.000,0.000>"), &r));
        GrblStatusParser::merge(r, &s);
        QCOMPARE(s.mpos.x, 11.0);
        QCOMPARE(s.mpos.y, 12.0);
        QCOMPARE(s.wpos.x, 1.0);
    }

    void mergeKeepsOverrides()
    {
        MachineStatus s;
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Idle|MPos:0,0,0|FS:0,0|Ov:120,100,80>"), &r));
        GrblStatusParser::merge(r, &s);
        QVERIFY(GrblStatusParser::parse(QStringLiteral("<Idle|MPos:0,0,0|FS:0,0>"), &r));
        GrblStatusParser::merge(r, &s);
        QCOMPARE(s.ovFeed, 120);
        QCOMPARE(s.ovSpindle, 80);
    }

    void manufacturerVariant()
    {
        // Some vendor firmwares add fields or spaces; unknown fields are ignored.
        StatusReport r;
        QVERIFY(GrblStatusParser::parse(QStringLiteral(" <Idle|MPos:0.000,0.000|FS:0,0|Laser:on> "), &r));
        QCOMPARE(r.state, MachineState::Idle);
        QCOMPARE(r.mpos->z, 0.0);
    }
};

QTEST_GUILESS_MAIN(TestGrblStatusParser)
#include "tst_grblstatusparser.moc"
