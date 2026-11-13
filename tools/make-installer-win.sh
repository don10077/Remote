#!/usr/bin/env bash
# Собирает установщик Windows (NSIS) из папки, разложенной tools/deploy-win.sh.
#
# Использование (из build.cmd или CI, в оболочке MSYS2 MINGW64):
#   bash tools/make-installer-win.sh <каталог сборки> <папка с программой> <папка результата>
#   например: bash tools/make-installer-win.sh build/win-release dist/win dist
#
# Результат: <папка результата>/Harmony-<версия>-setup.exe.
# Нужен makensis (пакет MSYS2 mingw-w64-x86_64-nsis).

set -euo pipefail

build="$1"
src="$2"
mkdir -p "$3"
out="$(cd "$3" && pwd)"

# Версия — та же, что в project(VERSION …): из кеша CMake.
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt" | tr -d '\r')"
setup="$out/Harmony-$version-setup.exe"

# makensis: из MSYS2, иначе — обычная установка NSIS (она есть на раннерах GitHub).
makensis="$(command -v makensis || echo "/c/Program Files (x86)/NSIS/makensis.exe")"

# makensis — программа Windows: пути ей нужны виндовые и абсолютные
# (относительные она считает от папки .nsi).
"$makensis" -V2 \
    "-DVERSION=$version" \
    "-DSRCDIR=$(cygpath -w "$(cd "$src" && pwd)")" \
    "-DOUTFILE=$(cygpath -w "$setup")" \
    "$(cygpath -w "$(dirname "$0")/../packaging/windows/harmony.nsi")"

echo "Готово: $setup"
