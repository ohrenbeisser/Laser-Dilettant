// Dialog "Einstellungen" mit Reitern (siehe docs/planung.md 3.12 und 3.35): Allgemein (Sprache, Design, Einheiten),
// Anzeige (Farben, Raster, Deckkraft), Dateien (Sicherung, Standardordner), Laser (Job-Ursprung, Schwachlicht) und
// Bedienung (Jog-Schritte, Konsole).
#pragma once

#include <QDialog>
#include <QPushButton>

#include "appsettings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace ld::app {

class OriginSelector;
class NotificationController;

// Knopf, der eine Farbe zeigt und beim Klicken einen Farbwähler öffnet.
class ColorButton : public QPushButton {
    Q_OBJECT

public:
    explicit ColorButton(const QColor& color, QWidget* parent = nullptr);
    QColor color() const { return color_; }
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);   // der Bediener hat im Farbwähler eine andere Farbe gewählt

private:
    QColor color_;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // currentOrigin: Job-Ursprung des geladenen Bildes (ohne Bild: der Standard). Der Dialog zeigt ihn an.
    explicit SettingsDialog(ld::core::JobOrigin currentOrigin, QWidget* parent = nullptr);

    // Für den Knopf "Testnachricht senden" im Reiter Benachrichtigung (ohne ihn bleibt der Knopf gesperrt).
    void setNotifier(NotificationController* notifier);

    // Der gewählte Job-Ursprung und ob er sich gegenüber dem angezeigten geändert hat. Er gilt für das
    // aktuelle Bild und als Standard für neue Bilder; beides setzt der Aufrufer (siehe MainWindow).
    ld::core::JobOrigin jobOrigin() const;
    bool jobOriginChanged() const;
    // Hat der Bediener die Sprache geändert? (Sie wirkt erst nach einem Neustart; das MainWindow weist darauf hin.)
    bool languageChanged() const;

    void accept() override;
    void reject() override;   // nimmt die schon sichtbaren Änderungen (Design, Einheiten, Anzeige) wieder zurück

private:
    void setColorControls(const DisplayColors& colors);
    DisplayColors colorsFromControls() const;
    void applyLive();   // Design, Einheiten und Anzeige sofort anwenden (wirken schon, solange der Dialog offen ist)

    QComboBox* designCombo_ = nullptr;
    QComboBox* lengthCombo_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    ColorButton* progressButton_ = nullptr;
    ColorButton* burnButton_ = nullptr;
    ColorButton* travelButton_ = nullptr;
    ColorButton* rapidButton_ = nullptr;
    ColorButton* imageBurnButton_ = nullptr;
    ColorButton* imagePaperButton_ = nullptr;
    QCheckBox* gridCheck_ = nullptr;
    ColorButton* gridColorButton_ = nullptr;
    QSlider* gridStrength_ = nullptr;
    QSlider* paperOpacity_ = nullptr;
    // Werte beim Öffnen, für "Abbrechen"
    DesignChoice initialDesign_ = DesignChoice::System;
    LengthUnit initialLength_ = LengthUnit::Millimeter;
    SpeedUnit initialSpeed_ = SpeedUnit::MmPerMin;
    ResolutionUnit initialResolution_ = ResolutionUnit::LinesPerMm;
    DisplayColors initialColors_;
    bool live_ = false;   // erst nach dem Aufbau des Dialogs wirken Änderungen sofort
    OriginSelector* originSelector_ = nullptr;
    ld::core::JobOrigin initialOrigin_ = ld::core::JobOrigin::TopLeft;
    QComboBox* languageCombo_ = nullptr;
    QLineEdit* imageDirEdit_ = nullptr;
    QLineEdit* projectDirEdit_ = nullptr;
    QLineEdit* gcodeDirEdit_ = nullptr;
    QLineEdit* ncDirEdit_ = nullptr;
    QString initialLanguage_;
    QDoubleSpinBox* jogStepSpins_[4] = {nullptr, nullptr, nullptr, nullptr};
    QComboBox* jogDefaultCombo_ = nullptr;
    QCheckBox* consoleStatusCheck_ = nullptr;
    QSpinBox* consoleLinesSpin_ = nullptr;
    QSpinBox* autosaveSpin_ = nullptr;
    QCheckBox* startWarningCheck_ = nullptr;
    QCheckBox* autoConnectCheck_ = nullptr;
    QSpinBox* weakMaxSpin_ = nullptr;
    QComboBox* weakAutoOffCombo_ = nullptr;
    // Reiter Benachrichtigung
    QCheckBox* ntfyEnabledCheck_ = nullptr;
    QLineEdit* ntfyServerEdit_ = nullptr;
    QLineEdit* ntfyTopicEdit_ = nullptr;
    QCheckBox* ntfyFinishedCheck_ = nullptr;
    QPushButton* ntfyTestButton_ = nullptr;
    QLabel* ntfyResultLabel_ = nullptr;
    NotificationController* notifier_ = nullptr;
    // Reiter Webzugriff
    QCheckBox* webEnabledCheck_ = nullptr;
    QSpinBox* webPortSpin_ = nullptr;
    QCheckBox* webControlCheck_ = nullptr;
    QComboBox* webPreviewCombo_ = nullptr;
    QLabel* webPasswordLabel_ = nullptr;
    QString newWebPassword_;   // neues Passwort, gilt erst nach OK (nie im Klartext gespeichert)
};

}  // namespace ld::app
