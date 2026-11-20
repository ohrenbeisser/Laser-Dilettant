#!/usr/bin/env bash
# Baut das AppImage von Laser-Dilettant (Linux, x86_64).
#
# Aufruf im Projektordner (nach dem Release-Bau):
#   cmake --preset linux-release -DLD_BUILD_TESTS=OFF && cmake --build --preset linux-release
#   packaging/linux/make-appimage.sh
#
# Das Skript legt zuerst einen "AppDir" an (derselbe Aufbau wie im deb-Paket, dazu die xcb- und xkbcommon-Bibliotheken, die
# andere Distributionen nicht immer haben) und packt ihn dann mit "appimagetool" zu einer Datei. appimagetool wird NICHT
# von diesem Skript heruntergeladen: Es muss im Pfad liegen oder mit APPIMAGETOOL=/Pfad/zu/appimagetool angegeben werden
# (https://github.com/AppImage/appimagetool). Fehlt es, bleibt der fertige AppDir stehen und lässt sich mit
# <AppDir>/AppRun schon ausprobieren.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD=${BUILD:-build/linux-release}
OUT=${OUT:-$BUILD/appimage}
APPDIR=$(realpath -m "$OUT/AppDir")
VERSION=$(sed -n 's/^project(LaserDilettant VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
APPIMAGETOOL=${APPIMAGETOOL:-$(command -v appimagetool || true)}

echo "== 1/4 AppDir anlegen ($APPDIR)"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr >/dev/null 2>&1
APP="$APPDIR/usr/lib/laser-dilettant"

echo "== 2/4 Bibliotheken des Systems dazupacken, die nicht überall vorhanden sind"
# Fenstersystem-Bibliotheken (xcb, xkbcommon): auf manchen Distributionen fehlen sie oder haben andere Versionen.
# NICHT eingepackt werden: libc, OpenGL/EGL (Treiber des Rechners), Schriften und Desktop-Bibliotheken (fontconfig, freetype,
# glib, dbus, X11), TLS (libssl): Die kommen vom System, wie bei jedem AppImage üblich.
{
    ldd "$APP/bin/laser-dilettant"
    for f in "$APP"/plugins/*/*.so; do ldd "$f"; done
} | awk '/=> \//{print $3}' | grep -E '/(libxcb[^/]*|libxkbcommon[^/]*|libX11-xcb[^/]*)\.so' | sort -u | while read -r lib; do
    cp -L "$lib" "$APP/lib/"
    echo "   $(basename "$lib")"
done

echo "== 3/4 Starter, Symbol und AppRun"
cp "$APPDIR/usr/share/applications/laser-dilettant.desktop" "$APPDIR/laser-dilettant.desktop"
cp assets/logo/png/laser-dilettant-256.png "$APPDIR/laser-dilettant.png"
ln -sf laser-dilettant.png "$APPDIR/.DirIcon"
cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
# Startet das Programm aus dem AppImage; Qt findet seine Bibliotheken und Plugins neben dem Programm (qt.conf, RPATH).
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="$HERE"
exec "$HERE/usr/lib/laser-dilettant/bin/laser-dilettant" "$@"
RUN
chmod 755 "$APPDIR/AppRun"
# Das AppImage braucht keinen Verweis in /usr/bin und keine Pakettexte
rm -rf "$APPDIR/usr/bin" "$APPDIR/usr/share/doc" "$APPDIR/usr/share/man"

echo "== 4/4 AppImage packen"
if [ -z "$APPIMAGETOOL" ]; then
    echo "appimagetool nicht gefunden. Der AppDir ist fertig: $APPDIR/AppRun startet das Programm."
    echo "Zum Packen appimagetool bereitstellen (APPIMAGETOOL=/Pfad/appimagetool) und das Skript noch einmal starten."
    exit 3
fi
TARGET="$OUT/Laser-Dilettant-$VERSION-x86_64.AppImage"
# APPIMAGE_EXTRACT_AND_RUN: appimagetool selbst läuft auch ohne FUSE
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APPDIR" "$TARGET"
echo "Fertig: $TARGET ($(du -h "$TARGET" | cut -f1))"
