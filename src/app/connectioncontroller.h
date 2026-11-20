// ConnectionController: Verbindung zum Laser (zerlegt aus dem MainWindow, siehe planung.md 3.33).
//
// Er besitzt die GrblConnection und kümmert sich um alles, was mit dem Anschluss selbst zu tun hat:
//   * verbinden (Port wählen, öffnen), Auto-Verbindung und Baudrate testen,
//   * den Selbsttest nach dem Verbinden (GRBL-Version, $30 und Laser-Modus gegen das Maschinenprofil),
//   * Alarme (Dialog, "Alarm aufheben"), Verbindungsverlust, Fehler und "GRBL antwortet nicht",
//   * Nullpunkt setzen und anfahren.
// Das MainWindow zeigt nur den Zustand an (Leuchte, Beschriftungen, Schalter) und hört auf seine Signale.
#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/machinesetup.h"
#include "grbl/grblconnection.h"

class QWidget;

namespace ld::app {

// Ergebnis des Selbsttests: Probleme (Texte für den Bediener) und was sich daraus ergibt.
struct SelfTestResult {
    QStringList problems;
    bool powerMismatch = false;      // $30 weicht vom Maschinenprofil ab
    bool laserModeMissing = false;   // das Profil nutzt M4, GRBL steht aber auf $32 = 0
    int s30 = 0;
    bool laserMode = false;
    bool passed() const { return problems.isEmpty(); }
};
// Vergleicht die Einstellungen von GRBL ($$) mit dem Maschinenprofil.
SelfTestResult evaluateSelfTest(const QMap<int, QString>& settings, const ld::core::MachineSetup& machine);

class ConnectionController : public QObject {
    Q_OBJECT

public:
    // machine: das Maschinenprofil des Hauptfensters (die Referenz bleibt gültig, der Wert ändert sich mit dem Profil).
    ConnectionController(const ld::core::MachineSetup& machine, QWidget* dialogParent, QObject* parent = nullptr);

    ld::grbl::GrblConnection* grbl() const { return grbl_; }
    QString grblVersion() const { return grblVersion_; }

    // Verbindet mit dem Port zum Schlüssel (siehe portfactory.h) und der Baudrate.
    void connectTo(const QString& portKey, int baud);
    // Beim Programmstart: mit dem zuletzt benutzten Port verbinden, falls so eingestellt.
    void autoConnect();
    // Probiert die üblichen Baudraten am Port durch (nur wenn nicht verbunden).
    void runBaudTest(const QString& portKey);
    // Nullpunkt: "Hier ist Null" setzen bzw. dorthin fahren.
    void setZero();
    void goZero();
    // Fragt nach und hebt eine Alarmsperre auf ($X).
    void confirmUnlock();

signals:
    void info(const QString& text);                  // Konsole
    void error(const QString& text);                 // Konsole, als Fehler
    void message(const QString& text, int milliseconds);   // Statusleiste
    void stateChanged();                             // Zustand der Verbindung (nach der internen Behandlung)
    void versionChanged();                           // GRBL-Version gelesen oder gelöscht
    void alarmStateChanged(bool unlockPossible);     // Menüpunkt "Alarm aufheben" ein oder aus
    void profileMaxPowerRequested(int s30);          // Selbsttest: Profil auf $30 anpassen
    // Ein Problem, das auch aus der Ferne gemeldet werden soll (Alarm, Verbindung verloren, GRBL antwortet nicht): Titel und Text.
    void problem(const QString& title, const QString& text);
    void grblSettingsRequested();                    // Selbsttest: Dialog "GRBL-Parameter" öffnen
    void baudRateFound(int baud);                    // Baudrate gefunden (ins Profil schreiben)
    // Nach dem Verbinden ist alles gelesen: Version ($I), alle Einstellungen ($$) und die Koordinatensysteme ($#). Einmal je Verbindung
    // (für das Systemprotokoll).
    void firmwareReport(const QString& version, const QString& options, const QMap<int, QString>& settings,
                        const QStringList& parameters);

private:
    void onStateChanged();
    void onStatus(const ld::grbl::GrblStatus& status);
    void runSelfTest(const QMap<int, QString>& settings);
    void collectReport();
    void showAlarmDialog(const QString& text);

    ld::grbl::GrblConnection* grbl_ = nullptr;
    const ld::core::MachineSetup& machine_;
    QWidget* dialogParent_;
    bool selfTestPending_ = false;
    // Bericht über die Firmware (siehe firmwareReport): Was ist schon angekommen?
    QString reportVersion_, reportOptions_;
    QMap<int, QString> reportSettings_;
    QStringList reportParameters_;
    bool gotInfo_ = false, gotSettings_ = false, gotParameters_ = false, reportSent_ = false;
    QString grblVersion_;
    bool alarmDialogOpen_ = false;
    bool alarmDialogShown_ = false;   // für den laufenden Alarm wurde der Dialog schon gezeigt
};

}  // namespace ld::app
