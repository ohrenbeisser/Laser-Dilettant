#pragma once

#include <QWidget>

class AppSettings;
class QCheckBox;
class QComboBox;
class QSpinBox;
class NumberSpinBox;

// "Programm" section of the settings tab.
class ProgramSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ProgramSettingsPanel(AppSettings* settings, QWidget* parent = nullptr);

private:
    AppSettings* m_settings;
};
