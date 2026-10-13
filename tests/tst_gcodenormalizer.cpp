#include "gcode/GcodeNormalizer.h"

#include <QTest>

using GcodeNormalizer::Result;

class TestGcodeNormalizer : public QObject
{
    Q_OBJECT
private slots:
    void normalize_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::addColumn<int>("result");
        QTest::newRow("spaces") << "G1 X10 Y20" << "G1X10Y20" << int(Result::Ok);
        QTest::newRow("lower") << "g0 x1.5" << "G0X1.5" << int(Result::Ok);
        QTest::newRow("semicolon") << "G1 X1 ; comment" << "G1X1" << int(Result::Ok);
        QTest::newRow("paren") << "G1 (move) X1" << "G1X1" << int(Result::Ok);
        QTest::newRow("only comment") << "; just a comment" << "" << int(Result::Empty);
        QTest::newRow("paren only") << "(header)" << "" << int(Result::Empty);
        QTest::newRow("empty") << "   " << "" << int(Result::Empty);
        QTest::newRow("percent") << "%" << "" << int(Result::Empty);
        QTest::newRow("tabs") << "\tM3\tS100\r" << "M3S100" << int(Result::Ok);
        QTest::newRow("too long") << QString(81, QLatin1Char('X')) << QString(81, QLatin1Char('X')) << int(Result::TooLong);
        QTest::newRow("80 is fine") << QString(80, QLatin1Char('X')) << QString(80, QLatin1Char('X')) << int(Result::Ok);
    }
    void normalize()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QFETCH(int, result);
        QString n;
        QCOMPARE(int(GcodeNormalizer::normalize(in, &n)), result);
        QCOMPARE(n, out);
    }

    void normalizeAll()
    {
        QStringList out;
        int bad = 0;
        QVERIFY(GcodeNormalizer::normalizeAll({ "G21", "", "; c", "G1 X1" }, &out, &bad));
        QCOMPARE(out, QStringList({ "G21", "G1X1" }));
        QVERIFY(!GcodeNormalizer::normalizeAll({ "G21", QString(90, QLatin1Char('Y')) }, &out, &bad));
        QCOMPARE(bad, 2);
    }
};

QTEST_GUILESS_MAIN(TestGcodeNormalizer)
#include "tst_gcodenormalizer.moc"
