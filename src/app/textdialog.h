// Dialog "Schrift" (siehe docs/planung.md 3.22): Text eingeben, Schriftart und Einstellungen wählen, in der
// Vorschau (mit Linealen) ansehen und als Bild auf die Arbeitsfläche übernehmen. Der Dialog arbeitet mit einer
// TextSpec; das Hauptfenster baut daraus das Bild (core/textmodel).
#pragma once

#include <QDialog>
#include <QTimer>
#include <optional>

#include "core/machinesetup.h"
#include "core/textmodel.h"

class QButtonGroup;
class QDialogButtonBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace ld::app {

class LengthSpinBox;
class TextPreview;

class TextDialog : public QDialog {
    Q_OBJECT

public:
    // initial: vorhandener Text zum Bearbeiten; ohne Angabe beginnt ein neuer Text mit den zuletzt benutzten
    // Einstellungen (Schriftart, Höhe ...). machine: für die Warnung "größer als der Arbeitsbereich".
    TextDialog(const ld::core::MachineSetup& machine, const std::optional<ld::core::TextSpec>& initial,
               QWidget* parent = nullptr);

    // Die eingestellten Werte (innerhalb der Grenzen).
    ld::core::TextSpec spec() const;

    void accept() override;
    void done(int result) override;

private:
    void buildUi();
    void applySpec(const ld::core::TextSpec& spec);
    void scheduleUpdate();
    void update();
    void updateInfo();
    void saveSettings() const;

    ld::core::MachineSetup machine_;
    bool editing_ = false;

    QPlainTextEdit* textEdit_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QPushButton* boldButton_ = nullptr;
    QPushButton* italicButton_ = nullptr;
    LengthSpinBox* heightSpin_ = nullptr;
    QButtonGroup* alignGroup_ = nullptr;
    QSlider* letterSlider_ = nullptr;
    QSpinBox* letterSpin_ = nullptr;
    QSlider* lineSlider_ = nullptr;
    QSpinBox* lineSpin_ = nullptr;
    TextPreview* preview_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QPushButton* okButton_ = nullptr;
    QTimer updateTimer_;
    ld::core::TextShape shape_;
    bool loading_ = false;
};

}  // namespace ld::app
