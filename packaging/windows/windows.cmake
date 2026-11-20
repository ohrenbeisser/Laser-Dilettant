# Windows-Installer (NSIS) mit mitgeliefertem Qt. Bauen (Developer-Eingabeaufforderung, Qt für MSVC 2022):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DLD_BUILD_TESTS=OFF
#   cmake --build build && cd build && cpack -G NSIS
# Nicht lokal getestet: wird zuerst über GitHub Actions gebaut (.github/workflows).
install(TARGETS laser-dilettant RUNTIME DESTINATION .)

# windeployqt kopiert Qt-DLLs und Plugins neben die .exe
qt_generate_deploy_app_script(
    TARGET laser-dilettant
    OUTPUT_SCRIPT LD_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT "${LD_DEPLOY_SCRIPT}" DESTINATION .)
# Qt-Texte der Standarddialoge auf Deutsch
install(FILES "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_de.qm" DESTINATION translations)
# Laufzeit von Visual C++ (statt eines eigenen Installers dafür)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/MANUAL.md" DESTINATION doc)

set(CPACK_PACKAGE_NAME "Laser-Dilettant")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Ohrenbeisser")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bilder für einen GRBL-Diodenlaser aufbereiten und brennen")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "Laser-Dilettant-${PROJECT_VERSION}-Setup")
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Laser-Dilettant")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/logo/laser-dilettant.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/logo/laser-dilettant.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "laser-dilettant.exe")
set(CPACK_NSIS_DISPLAY_NAME "Laser-Dilettant")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ohrenbeisser/Laser-Dilettant")
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_PACKAGE_EXECUTABLES "laser-dilettant" "Laser-Dilettant")
include(CPack)
