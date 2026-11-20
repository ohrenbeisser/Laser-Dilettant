# "grbl" = Verbindung zum Laser: Anschluss, GRBL-Protokoll, Streaming, simulierter Controller.
# Kennt keine Widgets und keine Bildverarbeitung.
add_library(ld_grbl STATIC
    port.h
    weaklight.h
    weaklight.cpp
    baudtester.h
    baudtester.cpp
    grblparser.h
    grblparser.cpp
    grblconnection.h
    grblconnection.cpp
    serialportadapter.h
    serialportadapter.cpp
    simulatedgrbl.h
    simulatedgrbl.cpp
)
target_include_directories(ld_grbl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ld_grbl PUBLIC Qt6::Core Qt6::SerialPort)
