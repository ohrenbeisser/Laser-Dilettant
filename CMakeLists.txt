cmake_minimum_required(VERSION 3.21)

project(LaserDilettant
    VERSION 0.1.0
    DESCRIPTION "Open source laser engraver control and G-code generator"
    HOMEPAGE_URL "https://github.com/ohrenbeisser/Laser-Dilettant"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LD_BUILD_TESTS "Build unit tests" ON)
option(LD_BUILD_APP "Build the application (off: only core library and tests)" ON)
option(LD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Qt 6.5 or newer is recommended (6.8 LTS). 6.4 is accepted so the project
# also builds against the Qt packages shipped with Ubuntu 24.04 / Mint 22.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui SerialPort Concurrent)
if(LD_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Svg LinguistTools)
endif()
if(LD_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

if(MSVC)
    set(LD_WARNING_FLAGS /W4 /permissive- /utf-8)
    if(LD_WARNINGS_AS_ERRORS)
        list(APPEND LD_WARNING_FLAGS /WX)
    endif()
else()
    set(LD_WARNING_FLAGS -Wall -Wextra -Wpedantic)
    if(LD_WARNINGS_AS_ERRORS)
        list(APPEND LD_WARNING_FLAGS -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Core library: everything except main(), so the tests can link against it.
# ---------------------------------------------------------------------------
set(LD_CORE_SOURCES
    src/core/Resources.cpp src/core/Resources.h
    src/core/Units.cpp src/core/Units.h
    src/core/DeviceProfile.cpp src/core/DeviceProfile.h
    src/core/ProfileStore.cpp src/core/ProfileStore.h
    src/device/ITransport.h
    src/device/IDeviceDriver.h
    src/device/GrblTypes.h
    src/device/SerialTransport.cpp src/device/SerialTransport.h
    src/device/SimulatedGrbl.cpp src/device/SimulatedGrbl.h
    src/device/GrblCodes.cpp src/device/GrblCodes.h
    src/device/GrblStatusParser.cpp src/device/GrblStatusParser.h
    src/device/GrblSettings.cpp src/device/GrblSettings.h
    src/device/GrblStreamer.cpp src/device/GrblStreamer.h
    src/device/GrblDriver.cpp src/device/GrblDriver.h
    src/device/FocusLaser.cpp src/device/FocusLaser.h
    src/device/CommLog.cpp src/device/CommLog.h
    src/gcode/GcodeNormalizer.cpp src/gcode/GcodeNormalizer.h
    src/gcode/GcodeProgram.h
    src/firmware/AvrdudeRunner.cpp src/firmware/AvrdudeRunner.h
    resources/resources.qrc
)

set(LD_UI_SOURCES
    src/app/AppSettings.cpp src/app/AppSettings.h
    src/app/MainWindow.cpp src/app/MainWindow.h
    src/app/KeyboardFilter.cpp src/app/KeyboardFilter.h
    src/app/Theme.cpp src/app/Theme.h
    src/ui/ConnectionBar.cpp src/ui/ConnectionBar.h
    src/ui/WorkArea.cpp src/ui/WorkArea.h
    src/ui/JobBar.cpp src/ui/JobBar.h
    src/ui/StatusLine.cpp src/ui/StatusLine.h
    src/ui/CollapsibleSection.cpp src/ui/CollapsibleSection.h
    src/ui/NumberSpinBox.cpp src/ui/NumberSpinBox.h
    src/ui/StateColors.cpp src/ui/StateColors.h
    src/ui/tabs/ControlTab.cpp src/ui/tabs/ControlTab.h
    src/ui/tabs/ConsoleWidget.cpp src/ui/tabs/ConsoleWidget.h
    src/ui/tabs/PlaceholderTab.cpp src/ui/tabs/PlaceholderTab.h
    src/ui/tabs/SettingsTab.cpp src/ui/tabs/SettingsTab.h
    src/ui/tabs/ProfileEditor.cpp src/ui/tabs/ProfileEditor.h
    src/ui/tabs/GrblParameterPanel.cpp src/ui/tabs/GrblParameterPanel.h
    src/ui/tabs/FirmwarePanel.cpp src/ui/tabs/FirmwarePanel.h
    src/ui/tabs/ProgramSettingsPanel.cpp src/ui/tabs/ProgramSettingsPanel.h
    src/ui/dialogs/GrblSettingsDialog.cpp src/ui/dialogs/GrblSettingsDialog.h
    src/ui/dialogs/FirmwareDialog.cpp src/ui/dialogs/FirmwareDialog.h
    src/ui/dialogs/AboutDialog.cpp src/ui/dialogs/AboutDialog.h
)

# Device, G-code and data handling without user interface (used by the tests).
add_library(ld_core STATIC ${LD_CORE_SOURCES})
target_include_directories(ld_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ld_core PUBLIC cxx_std_20)
target_link_libraries(ld_core PUBLIC Qt6::Core Qt6::Gui Qt6::SerialPort Qt6::Concurrent)
target_compile_options(ld_core PRIVATE ${LD_WARNING_FLAGS})
target_compile_definitions(ld_core PUBLIC
    LD_VERSION="${PROJECT_VERSION}"
    QT_DISABLE_DEPRECATED_BEFORE=0x060000)

if(LD_BUILD_APP)
# User interface
add_library(ld_ui STATIC ${LD_UI_SOURCES})
target_link_libraries(ld_ui PUBLIC ld_core Qt6::Widgets Qt6::Svg)
target_compile_options(ld_ui PRIVATE ${LD_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(laser-dilettant WIN32 src/app/main.cpp)
target_link_libraries(laser-dilettant PRIVATE ld_ui)
target_compile_options(laser-dilettant PRIVATE ${LD_WARNING_FLAGS})
if(WIN32)
    set_target_properties(laser-dilettant PROPERTIES OUTPUT_NAME "Laser-Dilettant")
endif()

# Translations are compiled into the binary (resource prefix /i18n).
qt_add_translations(laser-dilettant
    TS_FILES i18n/laser-dilettant_de.ts i18n/laser-dilettant_en.ts
    SOURCES ${LD_CORE_SOURCES} ${LD_UI_SOURCES} src/app/main.cpp
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none)

install(TARGETS laser-dilettant RUNTIME DESTINATION bin)
endif()

if(LD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
