# Installieren und Pakete bauen (CPack): Linux (deb, AppImage, Flatpak über denselben Installationsschritt) und Windows (NSIS).
#
# Aufbau des deb-Pakets (Qt 6.8 kommt mit, weil die Systemversionen älter sind):
#   /usr/bin/laser-dilettant                          Verweis auf das Programm
#   /usr/lib/laser-dilettant/bin/laser-dilettant      das Programm (RPATH $ORIGIN/../lib)
#   /usr/lib/laser-dilettant/lib/                     Qt-Bibliotheken und was sie brauchen
#   /usr/lib/laser-dilettant/plugins/                 Qt-Plugins (Fenstersystem, Bildformate ...)
#   /usr/share/applications, icons, metainfo, doc     Starter, Symbole, Beschreibung, Handbuch
# Paket bauen:  cmake --preset linux-release -DLD_BUILD_TESTS=OFF && cmake --build --preset linux-release &&
#               cd build/linux-release && cpack -G DEB
include(GNUInstallDirs)

# Rechte wie bei Debian-Paketen: Ordner 755, Dateien 644 (Programme 755)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/windows/windows.cmake")
    return()
endif()
if(NOT UNIX OR APPLE)
    return()
endif()

# Qt mitliefern (deb, AppImage) oder nicht (Flatpak: Qt kommt dort aus der KDE-Laufzeit, das Programm liegt in /app/bin).
option(LD_BUNDLE_QT "Qt-Bibliotheken ins Paket legen (AUS für Flatpak)" ON)

set(LD_APP_DIR "${CMAKE_INSTALL_LIBDIR}/laser-dilettant")

# --- Programm und Qt ---
if(NOT LD_BUNDLE_QT)
    install(TARGETS laser-dilettant RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(LD_BUNDLE_QT)
install(TARGETS laser-dilettant RUNTIME DESTINATION "${LD_APP_DIR}/bin")
set_target_properties(laser-dilettant PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

# Qt-Bibliotheken und -Plugins neben das Programm legen. Das Deploy-Skript von Qt legt die Bibliotheken auf Linux an die
# falsche Stelle (/usr/lib), darum kopiert dieses kleine Skript selbst: erst die Plugins, dann alles, was Programm und Plugins
# laden (file(GET_RUNTIME_DEPENDENCIES)). Systembibliotheken (/lib, /usr/lib) bleiben draußen; sie holt das Paket über "Depends".
set(LD_QT_LIBDIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
set(LD_QT_PLUGINDIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
install(CODE "
    # file(INSTALL) hängt DESTDIR selbst an; zum Lesen und Schreiben ist der echte Pfad (mit DESTDIR) nötig
    set(app_dir \"\${CMAKE_INSTALL_PREFIX}/${LD_APP_DIR}\")
    set(real_dir \"\$ENV{DESTDIR}\${app_dir}\")
    # Plugins: Fenstersystem (xcb), Bildformate, SVG-Symbole, TLS für HTTPS (ntfy), Eingabe und OpenGL-Anbindung (keine GTK-Themen: das Programm hat sein eigenes Design)
    set(plugin_dirs platforms imageformats iconengines tls platforminputcontexts xcbglintegrations)
    set(plugin_files \"\")
    foreach(dir IN LISTS plugin_dirs)
        # Fenstersystem: nur xcb (läuft unter Wayland über XWayland); die anderen Plattform-Plugins braucht das Programm nicht
        if(dir STREQUAL \"platforms\")
            file(GLOB found \"${LD_QT_PLUGINDIR}/platforms/libqxcb.so\")
        else()
            file(GLOB found \"${LD_QT_PLUGINDIR}/\${dir}/*.so\")
        endif()
        foreach(file IN LISTS found)
            get_filename_component(name \"\${file}\" NAME)
            file(INSTALL \"\${file}\" DESTINATION \"\${app_dir}/plugins/\${dir}\" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
            list(APPEND plugin_files \"\${real_dir}/plugins/\${dir}/\${name}\")
        endforeach()
    endforeach()
    # Im Installationsskript sind diese Angaben nicht von selbst gesetzt (sonst bricht file(GET_RUNTIME_DEPENDENCIES) ab)
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM linux+elf)
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"${CMAKE_OBJDUMP}\")
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES \"${CMAKE_BINARY_DIR}/src/app/laser-dilettant\"
        MODULES \${plugin_files}
        RESOLVED_DEPENDENCIES_VAR resolved
        UNRESOLVED_DEPENDENCIES_VAR unresolved
        DIRECTORIES \"${LD_QT_LIBDIR}\"
        POST_EXCLUDE_REGEXES \"^/lib/\" \"^/lib64/\" \"^/usr/lib/\" \"^/usr/lib64/\")
    foreach(lib IN LISTS resolved)
        file(INSTALL \"\${lib}\" DESTINATION \"\${app_dir}/lib\" FOLLOW_SYMLINK_CHAIN PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endforeach()
    # Die kopierten Bibliotheken verkleinern (die ICU-Daten und -Bibliotheken von Qt kommen ungestrippt)
    file(GLOB copied \"\${real_dir}/lib/*.so*\")
    foreach(lib IN LISTS copied)
        if(NOT IS_SYMLINK \"\${lib}\")
            execute_process(COMMAND \"${CMAKE_STRIP}\" --strip-unneeded \"\${lib}\")
        endif()
    endforeach()
    foreach(lib IN LISTS unresolved)
        message(WARNING \"Nicht gefunden (kommt hoffentlich aus dem System): \${lib}\")
    endforeach()
    # Qt-eigene Texte der Standarddialoge (Schließen, Abbrechen, Ja, Nein ...) auf Deutsch; Englisch ist in Qt eingebaut
    file(INSTALL \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_de.qm\" DESTINATION \"\${app_dir}/translations\" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    # qt.conf: Qt findet seine Plugins neben dem Programm
    file(WRITE \"\${real_dir}/bin/qt.conf\" \"[Paths]\\nPrefix = ..\\nPlugins = plugins\\nLibraries = lib\\n\")
    file(CHMOD \"\${real_dir}/bin/qt.conf\" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
")

# /usr/bin/laser-dilettant als Verweis auf das Programm
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    file(CHMOD \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(CREATE_LINK \"../${LD_APP_DIR}/bin/laser-dilettant\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/laser-dilettant\" SYMBOLIC)
")

endif()   # LD_BUNDLE_QT

# --- Starter, Symbole, Beschreibung, Dokumentation ---
install(FILES linux/laser-dilettant.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES linux/laser-dilettant.metainfo.xml DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
        RENAME io.github.ohrenbeisser.LaserDilettant.metainfo.xml)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES "${CMAKE_SOURCE_DIR}/assets/logo/png/laser-dilettant-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps" RENAME laser-dilettant.png)
endforeach()
install(FILES "${CMAKE_SOURCE_DIR}/assets/logo/laser-dilettant-icon.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps" RENAME laser-dilettant.svg)
set(LD_DOC_DIR "${CMAKE_INSTALL_DATADIR}/doc/laser-dilettant")
find_program(LD_GZIP gzip)
if(LD_GZIP)
    # Änderungsprotokoll (changelog.gz) und Handbuchseite (man 1), beide mit gzip -9n gepackt
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LD_DOC_DIR}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1\")
        execute_process(COMMAND sh -c \"${LD_GZIP} -9n -c '${CMAKE_CURRENT_SOURCE_DIR}/linux/changelog' > '\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LD_DOC_DIR}/changelog.gz'\")
        execute_process(COMMAND sh -c \"${LD_GZIP} -9n -c '${CMAKE_CURRENT_SOURCE_DIR}/linux/laser-dilettant.1' > '\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1/laser-dilettant.1.gz'\")
        file(CHMOD \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LD_DOC_DIR}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc\" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        file(CHMOD \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LD_DOC_DIR}/changelog.gz\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1/laser-dilettant.1.gz\" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    ")
endif()
install(FILES linux/copyright DESTINATION "${LD_DOC_DIR}")
install(FILES "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/MANUAL.md" DESTINATION "${LD_DOC_DIR}")

# --- CPack: deb ---
set(CPACK_PACKAGE_NAME "laser-dilettant")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Ohrenbeisser")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bilder für einen GRBL-Diodenlaser aufbereiten und brennen")
set(CPACK_PACKAGE_DESCRIPTION
"Laser-Dilettant lädt Bilder, bereitet sie auf, erzeugt G-Code (Full,
Cutting, Linie) und schickt ihn über USB an einen Diodenlaser mit GRBL.
Mit Vorschau, Simulation, Materialtest, Kalibrierung, Meldungen aufs
Handy und einer Fernansicht im Heimnetz. Beta-Version.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_NAME "laser-dilettant")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ohrenbeisser <ohrenbeisser@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/ohrenbeisser/Laser-Dilettant")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
# Die Qt-Bibliotheken liegen im Paket. Gebraucht wird, was das Fenstersystem (xcb-Plugin) und Qt von außen holen.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6 (>= 2.38), libstdc++6 (>= 11), libgcc-s1, libgl1, libegl1, libopengl0, libfontconfig1, libfreetype6, libdbus-1-3, libx11-6, libx11-xcb1, libxkbcommon0, libxkbcommon-x11-0, libxcb1, libxcb-cursor0, libxcb-glx0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render0, libxcb-render-util0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-util1, libxcb-xfixes0, libxcb-xkb1, libglib2.0-0t64 | libglib2.0-0, libpng16-16t64 | libpng16-16, libssl3t64 | libssl3, libzstd1, zlib1g")
# Hinweis: libc6 >= 2.38 heißt Ubuntu 24.04 / Linux Mint 22 oder neuer. libssl3 braucht Qt Network zur Laufzeit für https (ntfy).
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "avrdude")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
include(CPack)
