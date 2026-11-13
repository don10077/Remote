#!/usr/bin/env bash
# Собирает для macOS самодостаточный Harmony.app и образ диска .dmg из готовой сборки.
#
# Использование: tools/make-macos.sh <каталог сборки> <папка результата>
#   например:    tools/make-macos.sh build/release dist
#
# Что делает:
#   1. cmake --install кладёт Harmony.app в <папка результата>/stage;
#   2. macdeployqt добавляет в бандл фреймворки и плагины Qt;
#   3. рядом с приложением в бандле — консольная harmony-cli и переводы qtbase_*.qm;
#   4. бандл подписывается ad hoc (без подписи Apple Silicon не запустит его);
#   5. Harmony.app и ярлык «Программы» упаковываются в Harmony-<версия>-macos.dmg.
#
# Qt ищется через qmake: переменная QMAKE или qmake6/qmake из PATH.

set -euo pipefail

build="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"

version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")"
qmake="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
qtBins="$("$qmake" -query QT_INSTALL_BINS)"
qtTranslations="$("$qmake" -query QT_INSTALL_TRANSLATIONS)"

# --- Бандл --------------------------------------------------------------------
stage="$out/stage"
rm -rf "$stage"
cmake --install "$build" --component harmony --prefix "$stage"
app="$stage/Harmony.app"

# CLI — без Qt, кладётся к исполняемому файлу приложения:
# Harmony.app/Contents/MacOS/harmony-cli.
cp "$build/cli/harmony-cli" "$app/Contents/MacOS/"

"$qtBins/macdeployqt" "$app" -verbose=1

# Переводы стандартных кнопок Qt («OK», «Отмена»). Программа ищет их по пути
# QLibraryInfo, а он в бандле задаётся qt.conf, который написал macdeployqt.
mkdir -p "$app/Contents/Resources/translations"
for lang in ru de en; do
    if [ -f "$qtTranslations/qtbase_$lang.qm" ]; then
        cp "$qtTranslations/qtbase_$lang.qm" "$app/Contents/Resources/translations/"
    fi
done
qtConf="$app/Contents/Resources/qt.conf"
[ -f "$qtConf" ] || printf '[Paths]\n' > "$qtConf"
# Пути в qt.conf бандла отсчитываются от Contents; секция [Paths] в файле единственная.
printf 'Translations = Resources/translations\n' >> "$qtConf"

# macdeployqt меняет пути в библиотеках, это ломает их подписи — подписываем заново.
# «-» — подпись ad hoc: без сертификата Apple, но arm64 без подписи не запускается.
cp "$(dirname "$0")/../LICENSE" "$app/Contents/Resources/LICENSE.txt"
mkdir -p "$app/Contents/Resources/licenses"
cp -R "$stage/share/licenses/harmony/." "$app/Contents/Resources/licenses/"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

# --- Образ диска --------------------------------------------------------------
# В окне образа — приложение и ярлык «Программы»: перетащить одно на другое.
ln -s /Applications "$stage/Applications"
dmg="$out/Harmony-$version-macos.dmg"
rm -f "$dmg"
hdiutil create -volname "Harmony $version" -srcfolder "$stage" -fs HFS+ -format UDZO "$dmg"

echo "Готово: $dmg"
