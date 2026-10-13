#include "ui/tabs/SettingsTab.h"

#include "ui/CollapsibleSection.h"
#include "ui/tabs/FirmwarePanel.h"
#include "ui/tabs/GrblParameterPanel.h"
#include "ui/tabs/ProfileEditor.h"
#include "ui/tabs/ProgramSettingsPanel.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QWidget* later(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setEnabled(false);
    l->setMargin(8);
    return l;
}
} // namespace

SettingsTab::SettingsTab(ProfileStore* profiles, GrblDriver* driver, AppSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_profileEditor(new ProfileEditor(profiles, driver))
    , m_parameters(new GrblParameterPanel(driver))
    , m_firmware(new FirmwarePanel(driver, settings, profiles))
{
    auto* content = new QWidget;
    auto* l = new QVBoxLayout(content);
    l->addWidget(new CollapsibleSection(tr("Device profile"), m_profileEditor, true, content));
    l->addWidget(new CollapsibleSection(tr("GRBL parameters"), m_parameters, false, content));
    l->addWidget(new CollapsibleSection(tr("Firmware"), m_firmware, false, content));
    l->addWidget(new CollapsibleSection(tr("Material test"),
                                        later(tr("The test grid for finding power and speed follows in stage 7.")),
                                        false, content));
    l->addWidget(new CollapsibleSection(tr("Material presets"),
                                        later(tr("Material presets follow in stage 3.")), false, content));
    l->addWidget(new CollapsibleSection(tr("Program"), new ProgramSettingsPanel(settings), false, content));
    l->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}
