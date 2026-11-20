// Linke Seitenleiste: Verbindung, Steuerung (Jog), Schwachlicht, Nullpunkt & Jog, Material.
// Aufbau wie im Mockup (docs/mockup/oberflaeche.html).
//
// M7: Die Profilauswahl zeigt die Maschinenprofile, "Z" folgt dem Profil, und der Abschnitt
// "Material" verwaltet die Materialprofile (siehe docs/planung.md 3.12). Die Verbindung und die
// Jog-Tasten bekommen ihre Funktion mit M4.
#pragma once

#include <QComboBox>
#include <QList>
#include <QStringList>
#include <QWidget>
#include <optional>

#include "core/material.h"
#include "core/profilestore.h"
#include "grbl/grblconnection.h"
#include "portfactory.h"

class QLabel;
class QSlider;
class QPlainTextEdit;
class QPushButton;

namespace ld::core {
class ImageDocument;
}

class QButtonGroup;
class QHBoxLayout;

namespace ld::app {

// Auswahlfeld, das beim Aufklappen meldet, damit die Port-Liste frisch gelesen werden kann.
class PortComboBox : public QComboBox {
    Q_OBJECT

public:
    using QComboBox::QComboBox;
    void showPopup() override
    {
        emit aboutToOpen();
        QComboBox::showPopup();
    }

signals:
    void aboutToOpen();
};

class LeftPanel : public QWidget {
    Q_OBJECT

public:
    // materialFolder: Ordner mit den Materialdateien (eine JSON-Datei pro Material).
    LeftPanel(ld::core::ImageDocument* document, const QString& materialFolder, QWidget* parent = nullptr);

    // Maschinenprofile in der Auswahl "Profil" anzeigen; active wird gewählt.
    void setProfiles(const QStringList& names, const QString& active);
    // Name des gewählten Materials (leer: keines). Wird im Projekt nur zur Info gespeichert.
    QString currentMaterialName() const { return loaded_ ? loaded_->name : QString(); }
    // Hat das aktive Profil eine Z-Achse? Sonst steht der Hinweis "Z ausgegraut" da.
    void setHasZAxis(bool hasZAxis);

    // ---- Verbindung (M4) ----
    // Port-Liste setzen; select ist der Schlüssel des anzuwählenden Eintrags (leer: der erste).
    void setPortChoices(const QList<PortChoice>& choices, const QString& select);
    QString currentPortKey() const;
    void setBaudRate(int baud);
    // Zeigt den Zustand der Verbindung (Knöpfe und Auswahlfelder).
    void setConnectionState(ld::grbl::GrblConnection::State state);

    // Jog-Tasten (X/Y) und "Hier ist Null"/"Zum Nullpunkt" freigeben oder sperren.
    void setMotionEnabled(bool jog, bool zero);
    // Die vier Jog-Schritte (mm, aufsteigend) und welcher vorgewählt ist; baut die Tasten neu auf (Einstellungen).
    void setJogSteps(const QList<double>& steps, int selected);
    // Beim Lauf: die Materialwahl sperren (sie würde die Gravur-Werte ändern).
    void setMaterialLocked(bool locked);
    // Speichert ein Material aus den gegebenen Werten (fragt nach dem Namen), zum Beispiel aus dem Materialtest.
    void saveMaterialFromOperation(const ld::core::Operation& operation, const QString& note);
    void setPositionText(const QString& text);

    // ---- Schwachlicht (M8) ----
    void setWeakLightRange(int maxPercent);                     // Obergrenze aus den Einstellungen
    void setWeakLightValues(int percent, int autoOffSeconds);
    void setWeakLightState(bool on, bool canToggle);

signals:
    void weakLightToggled(bool on);
    void weakLightPowerChanged(int percent);
    void weakLightAutoOffChanged(int seconds);
    void jogPressed(const QPointF& direction);   // aus Sicht des Bedieners
    void jogReleased();
    void stepChanged(double mm);
    void materialTestRequested();   // Knopf "Materialtest …" im Abschnitt Material
    void setZeroRequested();
    void goZeroRequested();
    void connectRequested(const QString& portKey, int baud);
    void disconnectRequested();
    void baudTestRequested(const QString& portKey);
    void portListRequested();        // Port-Liste soll neu gelesen werden
    void baudChanged(int baud);
    // Der Bediener hat ein anderes Maschinenprofil gewählt.
    void profileSelected(const QString& name);

private:
    QHBoxLayout* stepButtonsLayout_ = nullptr;
    QButtonGroup* stepGroup_ = nullptr;
    void buildMaterialSection(class AccordionSection* section);
    void refreshMaterials(const QString& select);
    void loadSelectedMaterial();
    void updateMaterialState();
    bool isMaterialModified() const;
    void saveMaterialAs();
    void saveMaterial();
    void renameMaterial();
    void deleteMaterial();

    ld::core::ImageDocument* document_;
    ld::core::ProfileStore materials_;
    std::optional<ld::core::MaterialProfile> loaded_;   // zuletzt geladenes/gespeichertes Material

    QComboBox* profileCombo_ = nullptr;
    PortComboBox* portCombo_ = nullptr;
    QComboBox* baudCombo_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* weakToggle_ = nullptr;
    QSlider* weakPower_ = nullptr;
    QLabel* weakPowerValue_ = nullptr;
    QComboBox* weakAutoOff_ = nullptr;
    bool updatingWeak_ = false;
    QPushButton* baudTestButton_ = nullptr;
    QWidget* materialSection_ = nullptr;
    QList<QPushButton*> jogButtons_;
    QList<QPushButton*> zeroButtons_;
    QList<QPushButton*> zButtons_;
    QLabel* positionLabel_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QComboBox* materialCombo_ = nullptr;
    QPlainTextEdit* noteEdit_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* saveAsButton_ = nullptr;
    QPushButton* renameButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    bool updatingProfiles_ = false;
};

}  // namespace ld::app
