#include "core/Resources.h"
#include "device/GrblCodes.h"
#include "device/GrblSettings.h"

#include <QTest>

class TestGrblSettings : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { ldInitResources(); }

    void parseLine()
    {
        int n = 0;
        QString v;
        QVERIFY(GrblSettings::parseLine(QStringLiteral("$130=400.000"), &n, &v));
        QCOMPARE(n, 130);
        QCOMPARE(v, QStringLiteral("400.000"));
        QVERIFY(GrblSettings::parseLine(QStringLiteral("$32=1 (laser mode)"), &n, &v));
        QCOMPARE(v, QStringLiteral("1"));
        QVERIFY(!GrblSettings::parseLine(QStringLiteral("$N0="), &n, &v));
        QVERIFY(!GrblSettings::parseLine(QStringLiteral("ok"), &n, &v));
    }

    void textRoundTrip()
    {
        GrblSettings s;
        s.set(0, QStringLiteral("10"));
        s.set(130, QStringLiteral("400.000"));
        s.set(32, QStringLiteral("1"));
        const QString text = s.toText(QStringLiteral("Laser-Dilettant\nbackup"));
        QVERIFY(text.startsWith(QStringLiteral("; Laser-Dilettant")));
        const GrblSettings r = GrblSettings::fromText(text);
        QCOMPARE(r.values(), s.values());
        QCOMPARE(r.number(130), 400.0);
        QCOMPARE(r.number(999, -1), -1.0);
    }

    void csv()
    {
        const auto rows = GrblCodes::parseCsv(QStringLiteral("\"a\",\"b, c\"\n\"1\",\"say \"\"hi\"\"\"\n"));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0], QStringList({ "a", "b, c" }));
        QCOMPARE(rows[1][1], QStringLiteral("say \"hi\""));
    }

    void codeTexts()
    {
        GrblCodes::setLanguage(QStringLiteral("en"));
        QCOMPARE(GrblCodes::errorShort(22), QStringLiteral("Undefined feed rate"));
        QVERIFY(GrblCodes::alarmText(1).contains(QStringLiteral("Hard limit")));
        QCOMPARE(GrblCodes::setting(32).name, QStringLiteral("Laser-mode enable"));
        GrblCodes::setLanguage(QStringLiteral("de"));
        QCOMPARE(GrblCodes::errorShort(22), QStringLiteral("Vorschub nicht festgelegt"));
        QCOMPARE(GrblCodes::setting(32).name, QStringLiteral("Lasermodus"));
        QVERIFY(GrblCodes::errorText(999).contains(QStringLiteral("999")));
    }

    void allRequiredSettingsExplained()
    {
        const QList<int> required = { 0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 20, 21, 22, 23, 24, 25, 26, 27,
                                      30, 31, 32, 100, 101, 102, 110, 111, 112, 120, 121, 122, 130, 131, 132 };
        for (const QString& lang : { QStringLiteral("de"), QStringLiteral("en") }) {
            GrblCodes::setLanguage(lang);
            for (int n : required) {
                QVERIFY2(GrblCodes::hasSetting(n), qPrintable(QStringLiteral("%1: $%2").arg(lang).arg(n)));
                QVERIFY(!GrblCodes::setting(n).description.isEmpty());
            }
            for (int e = 1; e <= 38; ++e)
                if (e < 18 || e > 19)
                    QVERIFY2(!GrblCodes::errorText(e).startsWith(QStringLiteral("Unknown")), qPrintable(QString::number(e)));
            for (int a = 1; a <= 10; ++a)
                QVERIFY(!GrblCodes::alarmText(a).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestGrblSettings)
#include "tst_grblsettings.moc"
