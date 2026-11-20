#include "firmwarefile.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QMap>
#include <QRegularExpression>

namespace ld::core {

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}
}  // namespace

FirmwareCheck checkFirmwareHex(const QByteArray& hexFile, int flashBytes)
{
    FirmwareCheck check;
    QMap<int, quint8> memory;   // Adresse -> Byte
    quint32 upperAddress = 0;   // aus Datensätzen vom Typ 02 und 04
    bool sawEof = false;
    int lineNumber = 0;

    const QList<QByteArray> lines = hexFile.split('\n');
    for (QByteArray line : lines) {
        ++lineNumber;
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (sawEof) {
            check.errors << tr("Nach dem Dateiende-Satz (Zeile %1) steht noch Inhalt.").arg(lineNumber);
            break;
        }
        if (line[0] != ':' || line.size() < 11 || (line.size() % 2) == 0) {
            check.errors << tr("Zeile %1 ist kein gültiger Intel-HEX-Satz.").arg(lineNumber);
            break;
        }
        const QByteArray raw = QByteArray::fromHex(line.mid(1));
        if (raw.size() * 2 != line.size() - 1) {   // fromHex lässt ungültige Zeichen still weg
            check.errors << tr("Zeile %1 enthält Zeichen, die keine Hexziffern sind.").arg(lineNumber);
            break;
        }
        const int count = static_cast<quint8>(raw[0]);
        const int address = (static_cast<quint8>(raw[1]) << 8) | static_cast<quint8>(raw[2]);
        const int type = static_cast<quint8>(raw[3]);
        if (raw.size() != count + 5) {
            check.errors << tr("Zeile %1: Länge passt nicht zur Längenangabe.").arg(lineNumber);
            break;
        }
        int sum = 0;
        for (const char c : raw) sum += static_cast<quint8>(c);
        if ((sum & 0xff) != 0) {
            check.errors << tr("Zeile %1: Prüfsumme stimmt nicht (die Datei ist beschädigt).").arg(lineNumber);
            break;
        }
        const QByteArray data = raw.mid(4, count);
        if (type == 0x00) {
            for (int i = 0; i < count; ++i) memory.insert(static_cast<int>(upperAddress) + address + i, static_cast<quint8>(data[i]));
        } else if (type == 0x01) {
            sawEof = true;
        } else if (type == 0x02 && count == 2) {
            upperAddress = ((static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1])) * 16u;
        } else if (type == 0x04 && count == 2) {
            upperAddress = ((static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1])) << 16;
        } else if (type == 0x03 || type == 0x05) {
            // Startadresse: für AVR ohne Bedeutung
        } else {
            check.errors << tr("Zeile %1: Satztyp %2 wird nicht unterstützt.").arg(lineNumber).arg(type);
            break;
        }
    }
    if (check.errors.isEmpty() && !sawEof) check.errors << tr("Der Dateiende-Satz fehlt (die Datei ist unvollständig).");
    if (check.errors.isEmpty() && memory.isEmpty()) check.errors << tr("Die Datei enthält keine Daten.");
    if (!check.errors.isEmpty()) return check;

    check.bytes = memory.lastKey() + 1;
    check.dataBytes = static_cast<int>(memory.size());
    QByteArray image(check.bytes, static_cast<char>(0xff));   // leerer Flash ist 0xFF
    for (auto it = memory.begin(); it != memory.end(); ++it) image[it.key()] = static_cast<char>(it.value());
    check.sha256 = QString::fromLatin1(QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex());

    if (check.bytes > flashBytes) {
        check.errors << tr("Die Firmware ist mit %1 Bytes größer als der Flash des Chips (%2 Bytes).").arg(check.bytes).arg(flashBytes);
    } else if (check.bytes > flashBytes - 2048) {
        check.warnings << tr("Die Firmware ist %1 Bytes groß und reicht in den Bereich des Bootloaders. Das Aufspielen kann scheitern "
                             "oder den Bootloader beschädigen.").arg(check.bytes);
    }
    if (memory.firstKey() != 0) {
        check.warnings << tr("Die Firmware beginnt nicht bei Adresse 0 (sondern bei %1). Eine Firmware für einen AVR beginnt "
                             "sonst bei 0.").arg(memory.firstKey());
    }

    // Sieht es nach GRBL aus? Die Begrüßung "Grbl 1.1h ['$' for help]" steht im Programm als Text.
    static const QRegularExpression grbl(QStringLiteral("Grbl (\\d+\\.\\d+[a-z]*)"));
    const QRegularExpressionMatch match = grbl.match(QString::fromLatin1(image));
    check.looksLikeGrbl = match.hasMatch();
    if (check.looksLikeGrbl) {
        check.grblVersion = match.captured(1);
    } else {
        check.warnings << tr("Im Programm steht keine GRBL-Begrüßung (\"Grbl x.y\"). Das sieht nicht nach GRBL aus.");
    }
    check.valid = check.errors.isEmpty();
    return check;
}

QStringList avrdudeArguments(const AvrdudeSettings& settings, AvrdudeMode mode)
{
    QStringList args;
    args << QStringLiteral("-p") << settings.part << QStringLiteral("-c") << settings.programmer << QStringLiteral("-P")
         << settings.port << QStringLiteral("-b") << QString::number(settings.baud);
    if (mode == AvrdudeMode::DryRun) {
        args << QStringLiteral("-n");   // nichts schreiben; avrdude verbindet sich und liest nur die Signatur
    } else if (mode == AvrdudeMode::Backup) {
        // "r" = lesen: Der Chip wird nur ausgelesen. Der Flash enthält auch den Bootloader am Ende.
        args << QStringLiteral("-U") << QStringLiteral("flash:r:%1:i").arg(settings.backupFlashFile);
        args << QStringLiteral("-U") << QStringLiteral("eeprom:r:%1:i").arg(settings.backupEepromFile);
    } else {
        args << QStringLiteral("-U") << QStringLiteral("flash:w:%1:i").arg(settings.hexFile);   // schreiben, danach prüfen
    }
    return args;
}

AvrdudeResult parseAvrdudeOutput(const QString& output, int exitCode)
{
    AvrdudeResult result;
    // avrdude 7 schreibt "device signature = 0x1e950f (probably m328p)", ältere Versionen "Device signature" (groß)
    static const QRegularExpression signature(QStringLiteral("device signature = 0x([0-9a-fA-F]{6})"),
                                              QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = signature.match(output);
    if (match.hasMatch()) {
        result.signatureFound = true;
        result.signature = match.captured(1).toInt(nullptr, 16);
        result.isAtmega328 = result.signature == kAtmega328Signature;
    }
    if (result.signatureFound && result.isAtmega328 && exitCode == 0) {
        result.summary = tr("Der Chip antwortet: ATmega328P (Signatur 0x1E950F). Der Bootloader spricht mit avrdude.");
    } else if (result.signatureFound && !result.isAtmega328) {
        result.summary = tr("Der Chip meldet die Signatur 0x%1. Das ist kein ATmega328P. Nicht aufspielen.")
                             .arg(match.captured(1).toUpper());
    } else if (output.contains(QLatin1String("not in sync"))) {
        result.summary = tr("Keine Antwort vom Bootloader (\"not in sync\"). Entweder hat das Board keinen USB-Bootloader oder die "
                            "Baudrate stimmt nicht. Mit der anderen Baudrate probieren.");
    } else if (exitCode != 0) {
        result.summary = tr("avrdude ist mit einem Fehler beendet (Code %1). Die Ausgabe zeigt den Grund.").arg(exitCode);
    } else {
        result.summary = tr("avrdude lief, meldete aber keine Chip-Signatur.");
    }
    return result;
}

}  // namespace ld::core
