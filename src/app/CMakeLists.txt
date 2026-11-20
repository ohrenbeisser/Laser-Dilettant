# "app" = die Oberfläche (Qt Widgets). Der Code liegt in der Bibliothek "ld_app" (damit die Oberflächen-Tests ihn
# benutzen können); daraus und aus main.cpp entsteht das Programm "laser-dilettant".
add_library(ld_app STATIC
    mainwindow.h
    mainwindow.cpp
    accordion.h
    accordion.cpp
    leftpanel.h
    leftpanel.cpp
    rightpanel.h
    rightpanel.cpp
    canvasview.h
    canvasview.cpp
    rulerwidget.h
    rulerwidget.cpp
    previewbar.h
    lockbutton.h
    materialtestdialog.h
    materialtestdialog.cpp
    calibrationdialog.h
    calibrationdialog.cpp
    textdialog.h
    textdialog.cpp
    textpreview.h
    textpreview.cpp
    theme.h
    theme.cpp
    previewbar.cpp
    progressoverlay.h
    progressoverlay.cpp
    previewview.h
    previewview.cpp
    imagedialog.h
    imagedialog.cpp
    appsettings.h
    appsettings.cpp
    unitspinboxes.h
    unitspinboxes.cpp
    displayutil.h
    originselector.h
    originselector.cpp
    machineprofiles.h
    machineprofiles.cpp
    machineprofilesdialog.h
    machineprofilesdialog.cpp
    settingsdialog.h
    settingsdialog.cpp
    projectcontroller.h
    projectcontroller.cpp
    consolewidget.h
    consolewidget.cpp
    jogcontroller.h
    jogcontroller.cpp
    grblsettingsdialog.h
    grblsettingsdialog.cpp
    portfactory.h
    portfactory.cpp
    aboutdialog.h
    aboutdialog.cpp
    jobcontroller.h
    jobcontroller.cpp
    connectioncontroller.h
    connectioncontroller.cpp
    contentcontroller.h
    contentcontroller.cpp
    tooldialogs.h
    tooldialogs.cpp
    protocolcontroller.h
    protocolcontroller.cpp
    protocoldialog.h
    protocoldialog.cpp
    firmwaredialog.h
    firmwaredialog.cpp
    firmwareupdatedialog.h
    firmwareupdatedialog.cpp
    notificationcontroller.h
    notificationcontroller.cpp
    webserver.h
    webserver.cpp
    remotecontroller.h
    remotecontroller.cpp
    helpwindow.h
    helpwindow.cpp
)
# Die Seite der Fernansicht (src/web) als eingebettete Ressourcen (Pfad im Programm: :/web/...). Der WebServer liefert sie aus.
set(WEB_DIR "${CMAKE_SOURCE_DIR}/src/web")
file(GLOB WEB_FONTS "${WEB_DIR}/fonts/*")
qt_add_resources(ld_app web_assets
    PREFIX "/web"
    BASE "${WEB_DIR}"
    FILES
        ${WEB_DIR}/index.html ${WEB_DIR}/app.js ${WEB_DIR}/theme.css ${WEB_DIR}/basis.css ${WEB_DIR}/farben.css
        ${WEB_DIR}/logo.svg ${WEB_DIR}/logo-dunkel.svg ${WEB_FONTS}
)
# Das Handbuch (MANUAL.md im Projektordner) und seine Bilder (manual/bilder) als eingebettete Ressourcen für die Hilfe
# (Pfade im Programm: :/help/MANUAL.md und :/help/manual/bilder/...).
file(GLOB HELP_IMAGES "${CMAKE_SOURCE_DIR}/manual/bilder/*.png")
qt_add_resources(ld_app help_assets
    PREFIX "/help"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES ${CMAKE_SOURCE_DIR}/MANUAL.md ${HELP_IMAGES}
)

target_include_directories(ld_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ld_app PUBLIC ld_core ld_grbl Qt6::Widgets Qt6::Network)

qt_add_executable(laser-dilettant main.cpp)

# Logo und Lizenztext für den Über-Dialog als eingebettete Ressourcen (Pfad im Programm: :/about/...).
set(ABOUT_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(ABOUT_LOGO "${CMAKE_SOURCE_DIR}/assets/logo/laser-dilettant-logo-dunkel.svg")
set_source_files_properties("${ABOUT_LICENSE}" PROPERTIES QT_RESOURCE_ALIAS LICENSE)
set_source_files_properties("${ABOUT_LOGO}" PROPERTIES QT_RESOURCE_ALIAS logo.svg)
qt_add_resources(laser-dilettant about_resources
    PREFIX "/about"
    FILES "${ABOUT_LICENSE}" "${ABOUT_LOGO}"
)

# Programm-Icon (Fenster, Taskleiste): die PNG-Größen aus assets/logo/png (Pfad im Programm: :/icons/<Größe>.png).
set(ICON_FILES "")
foreach(size 16 24 32 48 64 128 256 512)
    set(icon "${CMAKE_SOURCE_DIR}/assets/logo/png/laser-dilettant-${size}.png")
    set_source_files_properties("${icon}" PROPERTIES QT_RESOURCE_ALIAS ${size}.png)
    list(APPEND ICON_FILES "${icon}")
endforeach()
qt_add_resources(laser-dilettant icon_resources PREFIX "/icons" FILES ${ICON_FILES})

# Windows: das Icon der .exe (Explorer, Taskleiste) kommt aus einer Resource-Datei mit der ICO-Datei.
if(WIN32)
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RC}" "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/assets/logo/laser-dilettant.ico\"\n")
    target_sources(laser-dilettant PRIVATE "${APP_ICON_RC}")
endif()

target_link_libraries(laser-dilettant PRIVATE ld_app)

# Versionsnummer aus dem Projekt (CMakeLists.txt oben) ins Programm geben.
target_compile_definitions(laser-dilettant PRIVATE LD_VERSION="${PROJECT_VERSION}")

# Windows: kein schwarzes Konsolenfenster; macOS: App-Bundle.
set_target_properties(laser-dilettant PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Übersetzungen: Die Quelltexte im Code sind Deutsch; die englische Übersetzung (.ts)
# wird später befüllt. qt_add_translations erzeugt daraus .qm-Dateien und bettet sie ein.
qt_add_translations(laser-dilettant
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/laser-dilettant_en.ts
)
