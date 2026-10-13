#include "core/Units.h"

#include <QLocale>
#include <QTest>

class TestUnits : public QObject
{
    Q_OBJECT
private slots:
    void gcodeNumber_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<int>("decimals");
        QTest::addColumn<QString>("expected");
        QTest::newRow("integer") << 12.0 << 3 << "12";
        QTest::newRow("half") << 12.5 << 3 << "12.5";
        QTest::newRow("three decimals") << 1.2345 << 3 << "1.235";
        QTest::newRow("trailing zeros") << 0.100 << 3 << "0.1";
        QTest::newRow("negative") << -3.25 << 3 << "-3.25";
        QTest::newRow("negative zero") << -0.0001 << 3 << "0";
        QTest::newRow("zero decimals") << 1500.4 << 0 << "1500";
        QTest::newRow("large") << 123456.0 << 3 << "123456";
    }
    void gcodeNumber()
    {
        QFETCH(double, value);
        QFETCH(int, decimals);
        QFETCH(QString, expected);
        QCOMPARE(Units::gcodeNumber(value, decimals), expected);
    }

    void gcodeNumberIgnoresLocale()
    {
        QLocale::setDefault(QLocale(QLocale::German));
        QCOMPARE(Units::gcodeNumber(1.5), QStringLiteral("1.5"));
        QCOMPARE(Units::display(1.5, 2), QStringLiteral("1,50"));
        QLocale::setDefault(QLocale::c());
    }

    void parse()
    {
        bool ok = false;
        QCOMPARE(Units::parse(QStringLiteral("1,5"), &ok), 1.5);
        QVERIFY(ok);
        QCOMPARE(Units::parse(QStringLiteral(" 2.25 "), &ok), 2.25);
        QVERIFY(ok);
        Units::parse(QStringLiteral("abc"), &ok);
        QVERIFY(!ok);
    }

    void conversions()
    {
        QCOMPARE(Units::mmPerSecToMmPerMin(20), 1200.0);
        QCOMPARE(Units::percentToS(35, 1000), 350.0);
        QCOMPARE(Units::percentToS(50, 255), 127.5);
        QCOMPARE(Units::duration(3725), QStringLiteral("1:02:05"));
        QCOMPARE(Units::duration(59.6), QStringLiteral("0:01:00"));
    }
};

QTEST_GUILESS_MAIN(TestUnits)
#include "tst_units.moc"
