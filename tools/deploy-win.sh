#!/usr/bin/env bash
# Раскладывает собранное приложение в папку dist вместе со всем, что нужно для запуска
# без MSYS2: плагины Qt (platforms, styles, imageformats, iconengines, tls — для HTTPS при
# скачивании базы ИК-кодов) и DLL из MinGW.
#
# Использование (из build.cmd и CI): bash tools/deploy-win.sh <путь к exe> <папка dist> [ещё exe…]
#   ещё exe — например, harmony-cli.exe: кладётся рядом, его DLL тоже копируются.
# Нужен MSYSTEM=MINGW64, чтобы ldd находил библиотеки в /mingw64/bin.

set -euo pipefail

exe="$1"
out="$2"
shift 2

mkdir -p "$out"
cp -f "$exe" "$@" "$out/"

# Где лежат плагины Qt: спрашиваем у самого Qt, при неудаче берём путь MSYS2 по умолчанию.
plugins="$(qtpaths6 --query QT_INSTALL_PLUGINS 2>/dev/null \
        || qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null \
        || echo /mingw64/share/qt6/plugins)"
plugins="${plugins%$'\r'}"

for dir in platforms styles imageformats iconengines tls networkinformation; do
    [ -d "$plugins/$dir" ] || continue
    mkdir -p "$out/$dir"
    cp -u "$plugins/$dir"/*.dll "$out/$dir/"
done

# Переводы стандартных кнопок и диалогов Qt («OK», «Отмена») для языков программы.
# В MSYS2 они в пакете mingw-w64-x86_64-qt6-translations; нет пакета — кнопки будут по-английски.
translations="$(qtpaths6 --query QT_INSTALL_TRANSLATIONS 2>/dev/null \
        || qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null \
        || echo /mingw64/share/qt6/translations)"
translations="${translations%$'\r'}"
for lang in ru de en; do
    if [ -f "$translations/qtbase_$lang.qm" ]; then
        mkdir -p "$out/translations"
        cp -u "$translations/qtbase_$lang.qm" "$out/translations/"
    fi
done

# ldd показывает всё дерево зависимостей; копируем только библиотеки из MinGW,
# системные DLL Windows не нужны. cp -u обновляет файл, только если исходный новее.
# Ошибка ldd на отдельном файле не должна прерывать раскладку, поэтому «|| true».
{ find "$out" \( -name '*.exe' -o -name '*.dll' \) -print0 | xargs -0 -n1 ldd 2>/dev/null || true; } \
    | awk '$3 ~ /^\/mingw64\// { print $3 }' \
    | sort -u \
    | while read -r dll; do cp -u "$dll" "$out/"; done

cp "$(dirname "$0")/../LICENSE" "$out/LICENSE.txt"
echo "Разложено в $out: $(find "$out" -name '*.dll' | wc -l) DLL"

mkdir -p "$out/licenses/libconcord"
cp "$(dirname "$0")/../third_party/libconcord/"{COPYING,NOTICE.md} "$out/licenses/libconcord/"
