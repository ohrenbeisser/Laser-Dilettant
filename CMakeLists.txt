# Haupt-Build-Datei von Laser-Dilettant.
# Sie legt nur die gemeinsamen Einstellungen fest und bindet die Unterordner ein.
cmake_minimum_required(VERSION 3.21)

project(LaserDilettant VERSION 0.1.2 LANGUAGES C CXX)

# --- Sprache: C++20 (so entschieden), ohne Compiler-Sondererweiterungen ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json hilft VS Code (clangd / C++-Erweiterung) beim Auflösen von Includes.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Qt 6.8 finden ---
# Qt liegt nicht im System, sondern in ~/Qt (siehe README, Abschnitt "Bauen unter Linux").
# Der Pfad wird über CMAKE_PREFIX_PATH übergeben, das erledigt CMakePresets.json.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg SerialPort Network LinguistTools)   # Qt6::Test nur für die Tests (siehe tests/)

# Schaltet AUTOMOC (Qt-Metaobjekte), AUTORCC und AUTOUIC automatisch ein.
qt_standard_project_setup(REQUIRES 6.8)

# --- Fremdbibliotheken (liegen in third_party/, bauen ohne Internet) ---
add_subdirectory(third_party/potrace)

# --- Warnungen einschalten: hilft beim Lernen, Fehler früh zu sehen ---
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# --- Unterprojekte ---
add_subdirectory(src/core)   # Fachlogik (ohne Widgets, ohne Seriellport)
add_subdirectory(src/grbl)   # Verbindung zum Laser (GRBL, Streaming, Simulator)
add_subdirectory(src/app)    # Oberfläche (Qt Widgets)

add_subdirectory(packaging)   # Installieren und Pakete (CPack)

option(LD_BUILD_TESTS "Automatische Tests (Catch2) mitbauen" ON)
# Fehlt der Ordner tests/ (zum Beispiel in einem Quellpaket), werden die Tests einfach nicht gebaut.
if(LD_BUILD_TESTS AND NOT EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    message(STATUS "Der Ordner tests/ fehlt: Die Tests werden nicht gebaut.")
    set(LD_BUILD_TESTS OFF)
endif()
if(LD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
