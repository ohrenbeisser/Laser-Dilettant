#pragma once

#include "core/DeviceProfile.h"

#include <QWidget>

class GrblDriver;
class NumberSpinBox;
class ProfileStore;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// Editor for the active device profile (section 8.1).
class ProfileEditor : public QWidget
{
    Q_OBJECT
public:
    ProfileEditor(ProfileStore* store, GrblDriver* driver, QWidget* parent = nullptr);

    void setActiveProfile(const QString& id);
    void setEditingAllowed(bool allowed);

signals:
    // A different profile should become active (after new/copy/delete).
    void activateProfile(const QString& id);

private:
    void load(const DeviceProfile& p);
    DeviceProfile collect() const;
    void updateEnabled();
    void save();
    void newProfile();
    void copyProfile();
    void renameProfile();
    void deleteProfile();
    void takeFromDevice();
    void markDirty();

    ProfileStore* m_store;
    GrblDriver* m_driver;
    DeviceProfile m_current;
    bool m_loading = false;
    bool m_dirty = false;
    bool m_editingAllowed = true;

    QLabel* m_title;
    QLabel* m_builtinHint;
    QSpinBox* m_baud;
    NumberSpinBox* m_areaX;
    NumberSpinBox* m_areaY;
    QComboBox* m_origin;
    QComboBox* m_laserType;
    QComboBox* m_laserMode;
    NumberSpinBox* m_maxS;
    QCheckBox* m_homing;
    QCheckBox* m_airAssist;
    NumberSpinBox* m_focusPower;
    NumberSpinBox* m_maxSpeed;
    NumberSpinBox* m_jogSpeed;
    QPushButton* m_save;
    QPushButton* m_revert;
    QPushButton* m_new;
    QPushButton* m_copy;
    QPushButton* m_rename;
    QPushButton* m_delete;
    QPushButton* m_fromDevice;
};
