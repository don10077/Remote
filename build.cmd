@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem ===========================================================================
rem  Сборка Harmony под Windows одной командой: build.cmd (или двойной щелчок).
rem
rem  Что делает:
rem    1. находит MSYS2 и добавляет его в PATH только для этого запуска;
rem    2. если чего-то не хватает (компилятор, CMake, Ninja, Qt 6, GoogleTest) -
rem       обновляет MSYS2 (pacman -Syu) и ставит недостающие пакеты;
rem    3. при первом запуске настраивает сборку (cmake --preset);
rem    4. обновляет переводы (tools\updateTranslations.py), если есть Python;
rem    5. собирает Release и прогоняет тесты;
rem    6. кладёт harmony.exe со всеми DLL в dist\win - оттуда он запускается
rem       без MSYS2 и его можно копировать на другой компьютер;
rem    7. с параметром installer (build.cmd installer) ещё и собирает установщик
rem       dist\Harmony-<версия>-setup.exe (NSIS ставится через pacman сам).
rem
rem  Путь к MSYS2 можно переопределить переменной окружения MSYS2_ROOT.
rem ===========================================================================

if not defined MSYS2_ROOT set "MSYS2_ROOT=D:\Install\MSYS2"
set "PRESET=win-release"
set "DIST=dist\win"
set "NSIS_PACKAGE=mingw-w64-x86_64-nsis"
set "PACKAGES=mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-qt6-translations mingw-w64-x86_64-gtest mingw-w64-x86_64-pugixml mingw-w64-x86_64-libzip mingw-w64-x86_64-pkgconf"

set "MINGW=%MSYS2_ROOT%\mingw64"
set "PATH=%MINGW%\bin;%MSYS2_ROOT%\usr\bin;%PATH%"

rem Работаем из папки, где лежит этот файл, откуда бы его ни запустили.
cd /d "%~dp0"

if not exist "%MSYS2_ROOT%\usr\bin\pacman.exe" (
    echo [ошибка] MSYS2 не найден в "%MSYS2_ROOT%".
    echo          Установите MSYS2 или задайте путь: set MSYS2_ROOT=C:\msys64
    goto :fail
)

rem --- 1. Проверка инструментов --------------------------------------------
set "MISSING="
if not exist "%MINGW%\include\zip.h" set "MISSING=1"
if not exist "%MINGW%\bin\pkg-config.exe" set "MISSING=1"
if not exist "%MINGW%\bin\g++.exe"   set "MISSING=1"
if not exist "%MINGW%\bin\cmake.exe" set "MISSING=1"
if not exist "%MINGW%\bin\ninja.exe" set "MISSING=1"
if not exist "%MINGW%\lib\cmake\Qt6Widgets\Qt6WidgetsConfig.cmake" set "MISSING=1"
if not exist "%MINGW%\lib\cmake\Qt6Svg\Qt6SvgConfig.cmake"           set "MISSING=1"
if not exist "%MINGW%\lib\cmake\GTest\GTestConfig.cmake"           set "MISSING=1"
rem Переводы стандартных кнопок Qt («Да», «Нет», «Отмена»).
if not exist "%MINGW%\share\qt6\translations\qtbase_ru.qm"         set "MISSING=1"
if not exist "%MINGW%\lib\cmake\pugixml\pugixml-config.cmake"     set "MISSING=1"

if defined MISSING (
    call :pacman_update
    if errorlevel 1 goto :fail
    echo --- Установка недостающих пакетов MSYS2 ---
    "%MSYS2_ROOT%\usr\bin\pacman.exe" -S --needed --noconfirm %PACKAGES%
    if errorlevel 1 (
        echo [ошибка] pacman не смог установить пакеты.
        goto :fail
    )
)

rem --- 2. Локальные пресеты (путь к тулчейну) ------------------------------
if not exist "CMakeUserPresets.json" (
    copy /Y "CMakeUserPresets.json.example" "CMakeUserPresets.json" >nul
)

rem --- 3. Настройка (только при первом запуске) ----------------------------
rem Дальше CMake сам перенастраивает сборку, когда меняются CMakeLists.txt.
if not exist "build\%PRESET%\CMakeCache.txt" (
    echo --- Настройка сборки ---
    cmake --preset %PRESET%
    if errorlevel 1 goto :fail
)

rem --- 4. Переводы ------------------------------------------------------------
rem Новые строки tr() попадают в harmony_ru.ts пустыми (type="unfinished"),
rem готовые переводы сохраняются. Без Python шаг пропускается: сборке он не нужен.
echo --- Переводы ---
set "PYTHON="
python --version >nul 2>&1 && set "PYTHON=python"
if not defined PYTHON py -3 --version >nul 2>&1 && set "PYTHON=py -3"
if not defined PYTHON (
    echo [предупреждение] Python не найден, harmony_ru.ts не обновлён.
    goto :build
)
%PYTHON% tools\updateTranslations.py
if errorlevel 1 echo [предупреждение] updateTranslations.py завершился с ошибкой, harmony_ru.ts мог не обновиться.

:build
rem --- 5. Сборка и тесты ---------------------------------------------------
echo --- Сборка ---
cmake --build --preset %PRESET%
if errorlevel 1 goto :fail

echo --- Тесты ---
ctest --preset %PRESET%
if errorlevel 1 goto :fail

rem --- 6. Раскладка в dist -------------------------------------------------
echo --- Копирование в %DIST% ---
set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "bash tools/deploy-win.sh 'build/%PRESET%/app/harmony.exe' '%DIST:\=/%' 'build/%PRESET%/cli/harmony-cli.exe'"
if errorlevel 1 (
    echo [ошибка] Не удалось скопировать в %DIST%.
    echo          Если harmony.exe сейчас запущен - закройте его и повторите.
    goto :fail
)

rem --- 7. Установщик (build.cmd installer) -------------------------------------
if /i not "%~1"=="installer" goto :done
if not exist "%MINGW%\bin\makensis.exe" (
    call :pacman_update
    if errorlevel 1 goto :fail
    echo --- Установка NSIS ---
    "%MSYS2_ROOT%\usr\bin\pacman.exe" -S --needed --noconfirm %NSIS_PACKAGE%
    if errorlevel 1 goto :fail
)
echo --- Установщик ---
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "bash tools/make-installer-win.sh 'build/%PRESET%' '%DIST:\=/%' dist"
if errorlevel 1 goto :fail

:done
echo.
echo [готово] %DIST%\harmony.exe
if /i "%~1"=="installer" echo [готово] установщик в папке dist
call :maybe_pause
exit /b 0

:fail
echo.
echo [ошибка] Сборка не удалась, подробности выше.
call :maybe_pause
exit /b 1

rem Полное обновление MSYS2 перед установкой: -S без -Syu даёт частичное
rem обновление (новые пакеты при старых библиотеках), которое MSYS2 не поддерживает.
rem Если обновилось ядро (pacman, msys2-runtime), первый проход на этом
rem заканчивается и закрывает процессы MSYS2; второй проход ставит остальное.
:pacman_update
echo --- Обновление MSYS2 (pacman -Syu) ---
"%MSYS2_ROOT%\usr\bin\pacman.exe" -Syu --noconfirm
"%MSYS2_ROOT%\usr\bin\pacman.exe" -Syu --noconfirm
if errorlevel 1 (
    echo [ошибка] pacman -Syu не смог обновить MSYS2.
    echo          Закройте все окна MSYS2 и запустите build.cmd ещё раз.
    echo          Если не поможет - обновите вручную в окне MSYS2: pacman -Syu
    exit /b 1
)
exit /b 0

rem Пауза, чтобы окно не закрылось сразу, если файл запущен двойным щелчком.
:maybe_pause
echo %cmdcmdline% | findstr /i /c:"%~nx0" >nul && pause
exit /b 0
