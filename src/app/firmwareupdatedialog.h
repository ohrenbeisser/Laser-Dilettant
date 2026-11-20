// Firmware-Update (aus dem Firmware-Dialog): Firmware-Datei (.hex) wählen und prüfen, Warnhinweise, und ein Trockenlauf,
// der den Chip über den Bootloader anspricht, aber NICHTS schreibt.
//
// Aufgespielt wird mit dem externen Programm "avrdude" (QProcess), nicht mit eigenem Code. Das echte Aufspielen ist
// noch NICHT freigegeben (kWriteReleased in der .cpp): Erst soll der Trockenlauf am Laser zeigen, dass der Bootloader
// ansprechbar ist. Die Bestätigung (Haken und Eingabe "AUFSPIELEN") und die Prüfung der Datei sind schon da.
#pragma once

#include <QDialog>
#include <QString>
#include <functional>

#include "core/firmwarefile.h"
#include "core/protocol.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTimer;

namespace ld::grbl {
class GrblConnection;
}

namespace ld::app {

// Was der Dialog vom Hauptfenster braucht (damit er ohne MainWindow-Kenntnis auskommt).
struct FirmwareUpdateContext {
    QString portKey;                                 // Port des Lasers ("ttyUSB0"; "simulator" = kein echter Laser)
    QString currentVersion;                          // aus "$I" (leer, wenn nicht gelesen)
    QString reportText;                              // der Firmware-Bericht, zum Sichern vor dem Update
    std::function<void(ld::core::LogLevel, const QString& message, const QString& details)> log;   // Systemprotokoll
    std::function<void()> reconnect;                 // nach dem Lauf wieder mit dem Laser verbinden
};

// Pfad des Anschlusses für avrdude: unter Linux "/dev/ttyUSB0", unter Windows "COM3".
QString avrdudePortPath(const QString& portKey);

class FirmwareUpdateDialog : public QDialog {
    Q_OBJECT

public:
    FirmwareUpdateDialog(ld::grbl::GrblConnection* grbl, const FirmwareUpdateContext& context, QWidget* parent = nullptr);

    void done(int result) override;   // solange avrdude läuft, lässt sich der Dialog nicht schließen

private:
    void chooseFile();
    void checkFile();
    void saveReport();
    void startBackup();
    void updateButtons();
    void runAvrdude(ld::core::AvrdudeMode mode);
    void onFinished(int exitCode);
    void note(ld::core::LogLevel level, const QString& message, const QString& details = QString());

    ld::grbl::GrblConnection* grbl_;
    FirmwareUpdateContext context_;
    ld::core::FirmwareCheck check_;
    bool fileChecked_ = false;
    ld::core::AvrdudeMode mode_ = ld::core::AvrdudeMode::DryRun;

    QLineEdit* fileEdit_;
    QPlainTextEdit* checkText_;
    QLineEdit* avrdudeEdit_;
    QComboBox* baudBox_;
    QCheckBox* understood_;
    QLineEdit* confirmEdit_;
    QPushButton* dryRunButton_;
    QPushButton* backupButton_;
    QPushButton* writeButton_;
    QPlainTextEdit* output_;
    QLabel* status_;
    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    QString outputText_;
    QString backupFlashPath_, backupEepromPath_;   // Ziele der laufenden Sicherung
};

}  // namespace ld::app
