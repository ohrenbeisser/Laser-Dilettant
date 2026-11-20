// Dialog "Maschinenprofile": Liste links (neu, kopieren, umbenennen, löschen), Formular rechts
// mit den Reitern Maschine, Laser und G-Code (siehe docs/planung.md 3.12).
// Alle Änderungen gelten erst mit "OK"; "Abbrechen" verwirft sie.
#pragma once

#include <QDialog>
#include <vector>

#include "core/machinesetup.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;

namespace ld::app {

class LengthSpinBox;
class MachineProfiles;
class SpeedSpinBox;

class MachineProfilesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MachineProfilesDialog(MachineProfiles* profiles, QWidget* parent = nullptr);

    void accept() override;

    // Name des Profils, das danach aktiv sein soll (das bisherige, auch wenn umbenannt; sonst das gewählte).
    QString activeNameAfter() const { return activeAfter_; }

private:
    struct Entry {
        QString originalName;   // leer bei neu angelegten Profilen
        ld::core::MachineSetup setup;
    };

    void buildUi();
    void refreshList(int select);
    void showEntry(int index);
    void readForm();            // Formular -> aktuelles Profil
    bool nameTaken(const QString& name, int exceptIndex) const;
    QString uniqueName(const QString& base) const;
    void newProfile();
    void copyProfile();
    void renameProfile();
    void deleteProfile();

    MachineProfiles* profiles_;
    std::vector<Entry> entries_;
    QStringList deleted_;       // Namen vorhandener Profile, die gelöscht werden
    QString activeBefore_;
    QString activeAfter_;
    int current_ = -1;
    bool loading_ = false;      // true, solange das Formular gefüllt wird

    QListWidget* list_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    LengthSpinBox* workAreaX_ = nullptr;
    LengthSpinBox* workAreaY_ = nullptr;
    QComboBox* rotation_ = nullptr;
    QCheckBox* mirror_ = nullptr;
    QCheckBox* hasZ_ = nullptr;
    QCheckBox* hasEndstops_ = nullptr;
    QDoubleSpinBox* dpi_ = nullptr;
    QLabel* pixelSizeLabel_ = nullptr;
    QComboBox* command_ = nullptr;
    QSpinBox* maxPower_ = nullptr;
    SpeedSpinBox* rapid_ = nullptr;
    SpeedSpinBox* maxSpeed_ = nullptr;
    QDoubleSpinBox* correctionX_ = nullptr;
    QDoubleSpinBox* correctionY_ = nullptr;
    QDoubleSpinBox* accelX_ = nullptr;
    QDoubleSpinBox* accelY_ = nullptr;
    QPlainTextEdit* start_ = nullptr;
    QPlainTextEdit* end_ = nullptr;
    QCheckBox* moveToZeroBefore_ = nullptr;
    QCheckBox* returnToZeroAfter_ = nullptr;
    QCheckBox* jogWithJogCommand_ = nullptr;
};

}  // namespace ld::app
