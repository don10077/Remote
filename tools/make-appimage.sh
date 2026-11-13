#!/usr/bin/env bash
# Собирает AppImage для Linux из готовой сборки: один файл, который запускается
# на любом дистрибутиве не старше того, где собран (Qt и библиотеки — внутри).
#
# Использование: tools/make-appimage.sh <каталог сборки> <папка результата>
#   например:    tools/make-appimage.sh build/release dist
#
# Что делает:
#   1. cmake --install кладёт приложение, CLI, ярлык и значки в AppDir/usr;
#   2. linuxdeploy с плагином Qt добавляет библиотеки и плагины Qt
#      (platforms/xcb, tls для HTTPS, стили, переводы qtbase_*.qm);
#   3. упаковывает всё в dist/Harmony-<версия>-x86_64.AppImage.
#
# Qt ищется через qmake: переменная QMAKE или qmake6/qmake из PATH.
# linuxdeploy (закреплённой версии, с проверкой sha256) скачивается один раз
# в <каталог сборки>/_appimage-tools.

set -euo pipefail

build="$(realpath "$1")"
out="$(realpath -m "$2")"

# Версия — та же, что в project(VERSION …): из кеша CMake.
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")"
arch="$(uname -m)"

# --- Инструменты --------------------------------------------------------------
# linuxdeploy исполняется в релизной сборке, поэтому версия закреплена, а файл
# сверяется с sha256. Переопределить можно переменными окружения с теми же именами.
# TODO: вписать тег релиза и sha256 файла *-x86_64.AppImage с этого релиза
#       (страницы Releases репозиториев linuxdeploy/linuxdeploy и linuxdeploy/linuxdeploy-plugin-qt).
LINUXDEPLOY_VERSION="${LINUXDEPLOY_VERSION:-TODO-tag}"
LINUXDEPLOY_SHA256="${LINUXDEPLOY_SHA256:-TODO-sha256}"
LINUXDEPLOY_QT_VERSION="${LINUXDEPLOY_QT_VERSION:-TODO-tag}"
LINUXDEPLOY_QT_SHA256="${LINUXDEPLOY_QT_SHA256:-TODO-sha256}"

tools="$build/_appimage-tools"
mkdir -p "$tools"
fetch() {
    # fetch <имя файла> <URL> <sha256>: скачать, если файла нет, и сверить хеш (каждый раз).
    local name="$1" url="$2" sha="$3"
    if ! [[ "$sha" =~ ^[0-9a-f]{64}$ ]]; then
        echo "Не задан sha256 для $name (сейчас: '$sha')." >&2
        echo "Впишите версию и sha256 в начало раздела «Инструменты» в $0 или задайте переменные окружения." >&2
        exit 1
    fi
    verify() { (cd "$tools" && echo "$sha  $name" | sha256sum -c --quiet - >/dev/null 2>&1); }
    # Файл от прежней версии (или continuous) не подходит — скачиваем заново.
    if [ -f "$tools/$name" ] && ! verify; then
        rm -f "$tools/$name"
    fi
    if [ ! -f "$tools/$name" ]; then
        # -f — ошибка HTTP прерывает скрипт; .part — недокачанный файл не примется за готовый.
        curl -fsSL -o "$tools/$name.part" "$url"
        mv "$tools/$name.part" "$tools/$name"
    fi
    if ! verify; then
        rm -f "$tools/$name"
        echo "sha256 файла $name не совпал с ожидаемым — файл удалён." >&2
        exit 1
    fi
    chmod +x "$tools/$name"
}
fetch "linuxdeploy-$arch.AppImage" \
      "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-$arch.AppImage" \
      "$LINUXDEPLOY_SHA256"
fetch "linuxdeploy-plugin-qt-$arch.AppImage" \
      "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$LINUXDEPLOY_QT_VERSION/linuxdeploy-plugin-qt-$arch.AppImage" \
      "$LINUXDEPLOY_QT_SHA256"

# Без FUSE (контейнеры, CI) AppImage-инструменты распаковываются и запускаются так.
export APPIMAGE_EXTRACT_AND_RUN=1

# Плагин Qt берёт пути к Qt у qmake; linuxdeploy ищет библиотеки через LD_LIBRARY_PATH.
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export LD_LIBRARY_PATH="$("$QMAKE" -query QT_INSTALL_LIBS)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# --- AppDir -------------------------------------------------------------------
appdir="$build/AppDir"
rm -rf "$appdir"
cmake --install "$build" --component harmony --prefix "$appdir/usr"

# --- Упаковка -----------------------------------------------------------------
mkdir -p "$out"
cd "$out"   # linuxdeploy кладёт AppImage в текущую папку
export LDAI_OUTPUT="Harmony-$version-$arch.AppImage"
"$tools/linuxdeploy-$arch.AppImage" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/harmony" \
    --executable "$appdir/usr/bin/harmony-cli" \
    --desktop-file "$appdir/usr/share/applications/harmony.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/harmony.png" \
    --plugin qt \
    --output appimage

echo "Готово: $out/$LDAI_OUTPUT"
