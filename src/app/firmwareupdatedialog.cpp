#include "firmwareupdatedialog.h"
#include "helpwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "grbl/grblconnection.h"

namespace ld::app {

using ld::core::AvrdudeMode;
using ld::core::LogLevel;

// Solange das hier false ist, schreibt der Dialog nie in den Laser: der Knopf "Aufspielen" bleibt gesperrt.
// Erst nach einem gelungenen Trockenlauf am echten Laser und nach Freigabe des Auftraggebers auf true setzen.
static constexpr bool kWriteReleased = false;

static constexpr int kMaxHexBytes = 4 * 1024 * 1024;   // eine .hex-Datei für 32 KB ist etwa 90 KB groß
static const char kConfirmWord[] = "AUFSPIELEN";

QString avrdudePortPath(const QString& portKey)
{
#ifdef Q_OS_WIN
    return portKey;
#else
    return portKey.startsWith(QLatin1Char('/')) ? portKey : QStringLiteral("/dev/") + portKey;
#endif
}

FirmwareUpdateDialog::FirmwareUpdateDialog(ld::grbl::GrblConnection* grbl, const FirmwareUpdateContext& context, QWidget* parent)
    : QDialog(parent), grbl_(grbl), context_(context)
{
    setWindowTitle(tr("Firmware-Update"));
    attachHelp(this, ld::core::helptopic::kFirmwareUpdate);   // F1 und Knopf "Hilfe" (Handbuch)
    resize(720, 760);
    auto* layout = new QVBoxLayout(this);

    auto* warning = new QLabel(
        tr("<b>Achtung: Ein Firmware-Update kann den Laser unbenutzbar machen.</b><ul>"
           "<li>Die vorhandene Firmware lässt sich über USB <b>nicht sichern</b>. Ein Update ist nicht rückgängig zu machen, wenn du "
           "die alte Firmware nicht als Datei hast. Wiederherstellen geht dann nur mit einem ISP-Programmierer.</li>"
           "<li>Ob die Datei zu <b>dieser Maschine</b> passt (Pinbelegung, Laser-Ausgang, Einstellungen), kann die App nicht "
           "erkennen. Das liegt bei dir. Nimm nur eine Firmware, die für dieses Board gedacht ist.</li>"
           "<li>Bricht die Verbindung oder der Strom während des Aufspielens ab, kann die Firmware beschädigt sein.</li>"
           "<li>Alle GRBL-Einstellungen ($$) können danach auf Werkseinstellung stehen. Sichere vorher den Bericht.</li>"
           "<li>Es gibt keine Garantie. Du machst das auf eigene Gefahr.</li></ul>"));
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("QLabel { border: 2px solid #c0392b; border-radius: 4px; padding: 6px; }"));
    layout->addWidget(warning);

    // --- Sicherung ---
    auto* backupRow = new QHBoxLayout;
    auto* backupLabel = new QLabel(tr("Empfohlen: vorher den Firmware-Bericht (Version und alle Einstellungen) sichern."));
    backupLabel->setWordWrap(true);
    auto* backupButton = new QPushButton(tr("Bericht speichern …"));
    backupRow->addWidget(backupLabel, 1);
    backupRow->addWidget(backupButton);
    layout->addLayout(backupRow);
    connect(backupButton, &QPushButton::clicked, this, &FirmwareUpdateDialog::saveReport);

    // --- Datei ---
    auto* fileRow = new QHBoxLayout;
    fileEdit_ = new QLineEdit;
    fileEdit_->setPlaceholderText(tr("Firmware-Datei (.hex)"));
    auto* browse = new QPushButton(tr("Datei wählen …"));
    fileRow->addWidget(fileEdit_, 1);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);
    connect(browse, &QPushButton::clicked, this, &FirmwareUpdateDialog::chooseFile);
    connect(fileEdit_, &QLineEdit::editingFinished, this, &FirmwareUpdateDialog::checkFile);

    checkText_ = new QPlainTextEdit;
    checkText_->setReadOnly(true);
    checkText_->setMaximumHeight(150);
    checkText_->setPlaceholderText(tr("Hier erscheint das Ergebnis der Prüfung der Datei."));
    layout->addWidget(checkText_);

    // --- Einstellungen von avrdude ---
    auto* settingsBox = new QGroupBox(tr("Aufspielen mit avrdude"));
    auto* settingsLayout = new QVBoxLayout(settingsBox);
    auto* avrdudeRow = new QHBoxLayout;
    avrdudeRow->addWidget(new QLabel(tr("Programm:")));
    avrdudeEdit_ = new QLineEdit(QSettings().value(QStringLiteral("firmware/avrdudePath"), QStringLiteral("avrdude")).toString());
    avrdudeRow->addWidget(avrdudeEdit_, 1);
    avrdudeRow->addWidget(new QLabel(tr("Baudrate des Bootloaders:")));
    baudBox_ = new QComboBox;
    baudBox_->addItem(QStringLiteral("115200"), 115200);
    baudBox_->addItem(QStringLiteral("57600"), 57600);
    const int savedBaud = QSettings().value(QStringLiteral("firmware/baud"), 115200).toInt();
    baudBox_->setCurrentIndex(savedBaud == 57600 ? 1 : 0);
    avrdudeRow->addWidget(baudBox_);
    settingsLayout->addLayout(avrdudeRow);
    auto* chipLabel = new QLabel(tr("Chip: ATmega328P (m328p), Protokoll: arduino. Die Baudrate hängt vom Bootloader ab "
                                    "(neu: 115200, alt: 57600). avrdude muss installiert sein (Linux: <tt>sudo apt install avrdude</tt>)."));
    chipLabel->setWordWrap(true);
    settingsLayout->addWidget(chipLabel);
    layout->addWidget(settingsBox);

    // --- Trockenlauf ---
    dryRunButton_ = new QPushButton(tr("Trockenlauf (Chip prüfen, nichts schreiben)"));
    layout->addWidget(dryRunButton_);
    connect(dryRunButton_, &QPushButton::clicked, this, [this] { runAvrdude(AvrdudeMode::DryRun); });

    // --- Sicherung der vorhandenen Firmware ---
    backupButton_ = new QPushButton(tr("Firmware sichern … (vorhandene Firmware aus dem Laser lesen, verändert nichts)"));
    layout->addWidget(backupButton_);
    connect(backupButton_, &QPushButton::clicked, this, &FirmwareUpdateDialog::startBackup);

    // --- Bestätigung und Aufspielen ---
    understood_ = new QCheckBox(tr("Ich habe die Warnungen gelesen und verstanden."));
    layout->addWidget(understood_);
    auto* confirmRow = new QHBoxLayout;
    confirmRow->addWidget(new QLabel(tr("Zur Bestätigung \"%1\" eintippen:").arg(QLatin1String(kConfirmWord))));
    confirmEdit_ = new QLineEdit;
    confirmRow->addWidget(confirmEdit_, 1);
    writeButton_ = new QPushButton(tr("Firmware aufspielen"));
    confirmRow->addWidget(writeButton_);
    layout->addLayout(confirmRow);
    if (!kWriteReleased) {
        auto* notReleased = new QLabel(tr("Das Aufspielen ist noch nicht freigegeben. Zuerst muss der Trockenlauf am Laser gelingen."));
        notReleased->setWordWrap(true);
        layout->addWidget(notReleased);
    }
    connect(understood_, &QCheckBox::toggled, this, &FirmwareUpdateDialog::updateButtons);
    connect(confirmEdit_, &QLineEdit::textChanged, this, &FirmwareUpdateDialog::updateButtons);
    connect(writeButton_, &QPushButton::clicked, this, [this] { runAvrdude(AvrdudeMode::Write); });

    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setPlaceholderText(tr("Ausgabe von avrdude"));
    layout->addWidget(output_, 1);
    status_ = new QLabel;
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (context_.portKey.isEmpty() || context_.portKey == QLatin1String("simulator"))
        status_->setText(tr("Mit dem Simulator ist kein Update möglich. Bitte erst mit dem echten Laser verbinden."));
    updateButtons();
}

void FirmwareUpdateDialog::done(int result)
{
    if (process_) return;   // avrdude läuft noch: nicht schließen
    QDialog::done(result);
}

void FirmwareUpdateDialog::note(LogLevel level, const QString& message, const QString& details)
{
    if (context_.log) context_.log(level, message, details);
}

void FirmwareUpdateDialog::updateButtons()
{
    const bool realPort = !context_.portKey.isEmpty() && context_.portKey != QLatin1String("simulator");
    const bool idle = process_ == nullptr;
    dryRunButton_->setEnabled(realPort && idle);
    backupButton_->setEnabled(realPort && idle);
    const bool confirmed = understood_->isChecked() && confirmEdit_->text().trimmed() == QLatin1String(kConfirmWord);
    writeButton_->setEnabled(kWriteReleased && realPort && idle && confirmed && fileChecked_ && check_.valid);
}

void FirmwareUpdateDialog::chooseFile()
{
    const QString start = QFileInfo(fileEdit_->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Firmware-Datei wählen"), start,
                                                      tr("Intel-HEX-Dateien (*.hex);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    fileEdit_->setText(path);
    checkFile();
}

void FirmwareUpdateDialog::checkFile()
{
    fileChecked_ = false;
    check_ = {};
    const QString path = fileEdit_->text().trimmed();
    if (path.isEmpty()) {
        checkText_->clear();
        updateButtons();
        return;
    }
    QFile file(path);
    if (QFileInfo(path).size() > kMaxHexBytes || !file.open(QIODevice::ReadOnly)) {
        checkText_->setPlainText(tr("FEHLER: Die Datei lässt sich nicht lesen oder ist viel zu groß für eine Firmware."));
        updateButtons();
        return;
    }
    check_ = ld::core::checkFirmwareHex(file.readAll());
    fileChecked_ = true;

    QStringList lines;
    lines << tr("Datei: %1").arg(QFileInfo(path).fileName());
    if (check_.errors.isEmpty()) {
        lines << tr("Größe: %1 Bytes (Flash des ATmega328P: %2 Bytes)").arg(check_.bytes).arg(ld::core::kAtmega328Flash);
        lines << tr("Prüfsumme (SHA-256): %1").arg(check_.sha256);
        if (check_.looksLikeGrbl) {
            lines << tr("Erkannt: GRBL %1 (im Laser jetzt: %2)")
                         .arg(check_.grblVersion, context_.currentVersion.isEmpty() ? tr("unbekannt") : context_.currentVersion);
        }
    }
    for (const QString& e : check_.errors) lines << tr("FEHLER: %1").arg(e);
    for (const QString& w : check_.warnings) lines << tr("Hinweis: %1").arg(w);
    if (check_.valid) lines << tr("Die Datei ist ein gültiges Intel-HEX. Ob sie zu dieser Maschine passt, kann die App nicht prüfen.");
    checkText_->setPlainText(lines.join(QLatin1Char('\n')));
    updateButtons();
}

void FirmwareUpdateDialog::saveReport()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Firmware-Bericht speichern"),
                                                      QStringLiteral("firmware-bericht.txt"), tr("Textdateien (*.txt)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(context_.reportText.toUtf8()) < 0) {
        QMessageBox::warning(this, tr("Bericht speichern"), tr("Die Datei lässt sich nicht schreiben:\n%1").arg(path));
        return;
    }
    status_->setText(tr("Bericht gespeichert: %1").arg(path));
}

void FirmwareUpdateDialog::startBackup()
{
    // Zielordner und Dateiname wählen: <Name>.hex für den Flash, <Name>-eeprom.hex für den EEPROM ($-Einstellungen).
    const QString suggestion = QStringLiteral("firmware-sicherung-%1.hex").arg(QDate::currentDate().toString(Qt::ISODate));
    QString path = QFileDialog::getSaveFileName(this, tr("Firmware sichern"), suggestion, tr("Intel-HEX-Dateien (*.hex)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1String(".hex"), Qt::CaseInsensitive)) path += QStringLiteral(".hex");
    backupFlashPath_ = path;
    backupEepromPath_ = path.left(path.size() - 4) + QStringLiteral("-eeprom.hex");
    runAvrdude(AvrdudeMode::Backup);
}

void FirmwareUpdateDialog::runAvrdude(AvrdudeMode mode)
{
    if (process_) return;
    if (mode == AvrdudeMode::Write && !kWriteReleased) return;   // doppelte Sicherung

    const QString program = avrdudeEdit_->text().trimmed();
    const QString resolved = QFileInfo(program).isAbsolute() ? program : QStandardPaths::findExecutable(program);
    if (resolved.isEmpty() || !QFileInfo(resolved).isExecutable()) {
        status_->setText(tr("avrdude wurde nicht gefunden (\"%1\"). Bitte installieren (Linux: sudo apt install avrdude) oder den "
                            "Pfad eintragen.").arg(program));
        return;
    }
    ld::core::AvrdudeSettings settings;
    settings.programPath = resolved;
    settings.port = avrdudePortPath(context_.portKey);
    settings.baud = baudBox_->currentData().toInt();
    settings.hexFile = fileEdit_->text().trimmed();
    settings.backupFlashFile = backupFlashPath_;
    settings.backupEepromFile = backupEepromPath_;

    const QString question = mode == AvrdudeMode::DryRun
        ? tr("Die Verbindung zum Laser wird getrennt, und avrdude spricht den Bootloader an. Dabei startet das Board neu. "
             "Es wird nichts geschrieben.\n\nFortfahren?")
        : mode == AvrdudeMode::Backup
        ? tr("Die Verbindung zum Laser wird getrennt, und avrdude liest Flash und EEPROM aus dem Chip in Dateien. Dabei startet "
             "das Board neu. Es wird nichts geschrieben.\n\nSicherung nach\n%1\n%2\n\nFortfahren?")
              .arg(backupFlashPath_, backupEepromPath_)
        : tr("Jetzt wird die Firmware in den Laser geschrieben. Das Kabel und der Strom dürfen nicht unterbrochen werden.\n\n"
             "Wirklich aufspielen?");
    if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes) return;

    QSettings().setValue(QStringLiteral("firmware/avrdudePath"), program);
    QSettings().setValue(QStringLiteral("firmware/baud"), settings.baud);

    // Der Anschluss muss frei sein: erst trennen (GRBL bekommt dabei Soft-Reset und M5).
    if (grbl_->state() != ld::grbl::GrblConnection::State::Disconnected) grbl_->disconnectDevice();

    mode_ = mode;
    outputText_.clear();
    output_->clear();
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] {
        const QString text = QString::fromLocal8Bit(process_->readAll());
        outputText_ += text;
        output_->moveCursor(QTextCursor::End);
        output_->insertPlainText(text);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) onFinished(-1);   // finished kommt dann nicht
    });
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        onFinished(status == QProcess::NormalExit ? code : -1);
    });
    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] { if (process_) process_->kill(); });
    timeout_->start(mode == AvrdudeMode::DryRun ? 60000 : mode == AvrdudeMode::Backup ? 180000 : 300000);

    const QStringList args = ld::core::avrdudeArguments(settings, mode);
    note(LogLevel::Info,
         mode == AvrdudeMode::DryRun   ? tr("Firmware-Update: Trockenlauf gestartet")
         : mode == AvrdudeMode::Backup ? tr("Firmware-Sicherung gestartet")
                                       : tr("Firmware-Update: Aufspielen gestartet"),
         tr("avrdude %1; Datei: %2; SHA-256: %3").arg(args.join(QLatin1Char(' ')), settings.hexFile.isEmpty() ? tr("(keine)") : settings.hexFile,
                                                       check_.sha256.isEmpty() ? tr("(keine)") : check_.sha256));
    status_->setText(tr("avrdude läuft …"));
    updateButtons();
    process_->start(settings.programPath, args);
    updateButtons();
}

void FirmwareUpdateDialog::onFinished(int exitCode)
{
    if (!process_) return;   // schon behandelt (errorOccurred und finished können beide kommen)
    QProcess* process = process_;
    process_ = nullptr;
    timeout_->stop();
    timeout_->deleteLater();
    timeout_ = nullptr;
    process->deleteLater();

    const ld::core::AvrdudeResult result = ld::core::parseAvrdudeOutput(outputText_, exitCode);
    QString summary = result.summary;
    bool backupOk = false;
    if (mode_ == AvrdudeMode::Backup) {
        // Sicherung prüfen: Datei lesen, als Intel-HEX prüfen, GRBL-Version und Prüfsumme nennen
        QFile file(backupFlashPath_);
        if (exitCode == 0 && file.open(QIODevice::ReadOnly)) {
            const ld::core::FirmwareCheck backup = ld::core::checkFirmwareHex(file.readAll());
            backupOk = backup.errors.isEmpty();
            if (backupOk) {
                summary = tr("Gesichert: %1 (Flash, %2 Bytes) und %3 (EEPROM). Im Flash erkannt: %4. SHA-256: %5")
                              .arg(QFileInfo(backupFlashPath_).fileName()).arg(backup.bytes)
                              .arg(QFileInfo(backupEepromPath_).fileName())
                              .arg(backup.looksLikeGrbl ? tr("GRBL %1").arg(backup.grblVersion) : tr("kein GRBL-Text"), backup.sha256);
            } else {
                summary = tr("Die gesicherte Datei ist nicht gültig: %1").arg(backup.errors.join(QLatin1Char(' ')));
            }
        } else {
            summary = tr("Die Sicherung ist fehlgeschlagen (Code %1). Die Ausgabe zeigt den Grund.").arg(exitCode);
        }
    } else if (mode_ == AvrdudeMode::Write) {
        summary = exitCode == 0 ? tr("Die Firmware wurde aufgespielt und geprüft.")
                                : tr("Aufspielen fehlgeschlagen (Code %1). Der Laser kann jetzt eine beschädigte Firmware haben.").arg(exitCode);
    }
    status_->setText(summary);
    const bool ok = exitCode == 0 && (mode_ == AvrdudeMode::Write || (mode_ == AvrdudeMode::Backup && backupOk) ||
                                      (mode_ == AvrdudeMode::DryRun && result.signatureFound && result.isAtmega328));
    note(ok ? LogLevel::Info : LogLevel::Warning,
         mode_ == AvrdudeMode::DryRun   ? tr("Firmware-Update: Trockenlauf beendet")
         : mode_ == AvrdudeMode::Backup ? tr("Firmware-Sicherung beendet")
                                        : tr("Firmware-Update: Aufspielen beendet"),
         summary);
    updateButtons();
    if (context_.reconnect) context_.reconnect();   // der Laser startet neu; die App verbindet wieder
}

}  // namespace ld::app
