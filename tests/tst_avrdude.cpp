#include "core/Resources.h"
#include "firmware/AvrdudeRunner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestAvrdude : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { ldInitResources(); }

    void arguments()
    {
        const QStringList r = AvrdudeRunner::readFlashArguments(QStringLiteral("/dev/ttyUSB0"), 57600, QStringLiteral("/tmp/b.hex"));
        QCOMPARE(r.join(QLatin1Char(' ')),
                 QStringLiteral("-c arduino -p m328p -P /dev/ttyUSB0 -b 57600 -U flash:r:%1:i")
                     .arg(QDir::toNativeSeparators(QStringLiteral("/tmp/b.hex"))));
        const QStringList w = AvrdudeRunner::writeFlashArguments(QStringLiteral("COM3"), 115200, QStringLiteral("g.hex"));
        QVERIFY(w.contains(QStringLiteral("-D")));
        QVERIFY(w.last().startsWith(QStringLiteral("flash:w:")));
    }

    void windowsPortName()
    {
        QCOMPARE(AvrdudeRunner::avrdudePortName(QStringLiteral("\\\\.\\COM12")), QStringLiteral("COM12"));
        QCOMPARE(AvrdudeRunner::avrdudePortName(QStringLiteral("/dev/ttyUSB0")), QStringLiteral("/dev/ttyUSB0"));
    }

    void bundledHexIsValid()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("grbl.hex"));
        QVERIFY(QFile::copy(QStringLiteral(":/firmware/grbl_v1.1h.hex"), path));
        QString error;
        QVERIFY2(AvrdudeRunner::validateHexFile(path, &error), qPrintable(error));
    }

    void brokenHex()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.hex"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(":100000000C94BB010C94CD010C94CD010C94C52E26\n:00000001FF\n"); // wrong checksum
        f.close();
        QString error;
        QVERIFY(!AvrdudeRunner::validateHexFile(path, &error));
        QVERIFY(!AvrdudeRunner::validateHexFile(dir.filePath(QStringLiteral("missing.hex")), &error));
    }
};

QTEST_GUILESS_MAIN(TestAvrdude)
#include "tst_avrdude.moc"
