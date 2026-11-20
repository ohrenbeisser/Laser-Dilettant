// Firmware-Datei prüfen und das Aufspielen vorbereiten (ohne Fenster und ohne Seriellport).
//
// Der Laser (GRBL 1.1z auf einem Arduino-artigen ATmega328P) bekommt eine neue Firmware als Intel-HEX-Datei (.hex).
// Hier steckt nur Fachlogik:
//   * checkFirmwareHex:   liest die Datei, prüft Zeilen und Prüfsummen, Größe und ob es nach GRBL aussieht;
//   * avrdudeArguments:   baut die Argumente für das externe Programm "avrdude" (Trockenlauf oder Aufspielen);
//   * parseAvrdudeOutput: liest aus der Ausgabe von avrdude, ob und welchen Chip es gefunden hat.
// Das Aufspielen selbst macht das Programm avrdude, nicht die App (planung.md, Abschnitt Firmware-Update).
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ld::core {

// ATmega328P: Flash-Größe in Bytes und Signatur (wie sie avrdude meldet).
constexpr int kAtmega328Flash = 32768;
constexpr int kAtmega328Signature = 0x1e950f;
// Ab dieser Größe könnte die Firmware in den Bereich eines großen Bootloaders (2 KB) reichen.
constexpr int kBootloaderWarnFrom = kAtmega328Flash - 2048;

struct FirmwareCheck {
    bool valid = false;            // gültiges Intel-HEX und passt in den Flash
    QStringList errors;            // Gründe, warum die Datei nicht benutzt werden darf
    QStringList warnings;          // Hinweise, die man lesen sollte
    int bytes = 0;                 // Größe des Programms (höchste Adresse + 1)
    int dataBytes = 0;             // tatsächlich belegte Bytes
    QString sha256;                // Prüfsumme der Daten (Hex), zum Wiedererkennen
    bool looksLikeGrbl = false;    // im Programm steht die Begrüßung "Grbl x.y"
    QString grblVersion;           // zum Beispiel "1.1h" (leer, wenn nicht erkannt)
};

// Prüft den Inhalt einer .hex-Datei. flashBytes: Größe des Flash des Chips.
FirmwareCheck checkFirmwareHex(const QByteArray& hexFile, int flashBytes = kAtmega328Flash);

struct AvrdudeSettings {
    QString programPath = QStringLiteral("avrdude");
    QString port;                                 // zum Beispiel "/dev/ttyUSB0"
    QString part = QStringLiteral("m328p");       // Chip
    QString programmer = QStringLiteral("arduino");   // Protokoll des Bootloaders
    int baud = 115200;                            // Bootloader: 115200 (Optiboot) oder 57600 (alt)
    QString hexFile;                              // nur zum Aufspielen
    QString backupFlashFile;                      // nur zum Sichern: Ziel für den Flash (Intel-HEX)
    QString backupEepromFile;                     // nur zum Sichern: Ziel für den EEPROM (dort liegen die $-Einstellungen)
};

enum class AvrdudeMode {
    DryRun,   // nur verbinden und die Chip-Signatur lesen; schreibt nichts ("-n")
    Write,    // Flash schreiben und prüfen
    Backup    // Flash und EEPROM aus dem Chip LESEN und in Dateien sichern (verändert den Laser nicht)
};

// Argumente für avrdude (ohne den Programmnamen).
QStringList avrdudeArguments(const AvrdudeSettings& settings, AvrdudeMode mode);

struct AvrdudeResult {
    bool signatureFound = false;
    int signature = 0;
    bool isAtmega328 = false;
    QString summary;   // ein Satz für den Bediener
};

// Wertet die Ausgabe von avrdude aus (Text und Beendigungscode).
AvrdudeResult parseAvrdudeOutput(const QString& output, int exitCode);

}  // namespace ld::core
