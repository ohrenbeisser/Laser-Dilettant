# "core" = Fachlogik ohne Fenster und ohne Seriellport.
# Darf Qt-Grundtypen (QtCore/QtGui, z. B. QImage, QPointF) benutzen.
# Als statische Bibliothek gebaut, damit Oberfläche und Tests denselben Code nutzen.
add_library(ld_core STATIC
    units.h
    units.cpp
    objecttransform.h
    objecttransform.cpp
    imageloader.h
    imageloader.cpp
    imagedocument.h
    imagedocument.cpp
    rulerscale.h
    rulerscale.cpp
    imagesettings.h
    imageprocessing.h
    imageprocessing.cpp
    rasterrunner.h
    rasterrunner.cpp
    engraving.h
    engraving.cpp
    vectorize.h
    vectorize.cpp
    svgmodel.h
    svgmodel.cpp
    calibration.h
    calibration.cpp
    textmodel.h
    textmodel.cpp
    machinesetup.h
    machinesetup.cpp
    gcode.h
    gcode.cpp
    ncfile.h
    ncfile.cpp
    ncloader.h
    ncloader.cpp
    materialtest.h
    materialtest.cpp
    gcoderunner.h
    gcoderunner.cpp
    gcodereader.h
    gcodereader.cpp
    pathdata.h
    pathdata.cpp
    pathrender.h
    pathrender.cpp
    simulation.h
    simulation.cpp
    pathpreview.h
    pathpreview.cpp
    movetimer.h
    movetimer.cpp
    material.h
    material.cpp
    protocol.h
    protocol.cpp
    firmwarefile.h
    firmwarefile.cpp
    ntfy.h
    ntfy.cpp
    http.h
    http.cpp
    webauth.h
    webauth.cpp
    helpdocument.h
    helpdocument.cpp
    profilestore.h
    profilestore.cpp
    project.h
    project.cpp
)

# Das übergeordnete Verzeichnis "src" wird Include-Pfad, damit man schreiben kann:
#   #include "core/units.h"
target_include_directories(ld_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(ld_core PUBLIC Qt6::Core Qt6::Gui Qt6::Svg)
target_include_directories(ld_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/nanosvg)
target_link_libraries(ld_core PRIVATE potrace_core)
