#pragma once

#include <QWidget>

class AppSettings;
class FirmwarePanel;
class GrblDriver;
class GrblParameterPanel;
class ProfileEditor;
class ProfileStore;

// Tab "Settings" with collapsible sections (section 9.4).
class SettingsTab : public QWidget
{
    Q_OBJECT
public:
    SettingsTab(ProfileStore* profiles, GrblDriver* driver, AppSettings* settings, QWidget* parent = nullptr);

    ProfileEditor* profileEditor() const { return m_profileEditor; }
    FirmwarePanel* firmwarePanel() const { return m_firmware; }

private:
    ProfileEditor* m_profileEditor;
    GrblParameterPanel* m_parameters;
    FirmwarePanel* m_firmware;
};
