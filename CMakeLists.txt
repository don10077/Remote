# Корневой CMake-файл проекта Harmony — редактор конфигурации пульта Logitech Harmony 900.
#
# Модули:
#   core      — модель конфигурации, форматы файлов, undo/redo. Чистый C++23, без Qt.
#   transport — связь с пультом (TCP поверх USB-сети пульта).
#   app       — графическое приложение на Qt 6 Widgets.
#   cli       — консольная утилита поверх core.
#   tests     — unit-тесты (GoogleTest).

cmake_minimum_required(VERSION 3.28)

project(harmony
    VERSION 0.3.0
    DESCRIPTION "Редактор конфигурации пульта Logitech Harmony 900"
    LANGUAGES C CXX
)

# --- Стандарт языка --------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json нужен clangd и IDE для подсказок.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Опции сборки ----------------------------------------------------------
option(HARMONY_BUILD_APP   "Собирать графическое приложение (нужен Qt 6)" ON)
option(HARMONY_BUILD_CLI   "Собирать консольную утилиту"                  ON)
option(HARMONY_BUILD_TESTS "Собирать unit-тесты"                          ON)
option(HARMONY_WARNINGS_AS_ERRORS "Считать предупреждения ошибками"       OFF)
option(HARMONY_FUZZ        "Собирать libFuzzer-цели (fuzz/, только Clang)" OFF)
option(HARMONY_USE_CCACHE  "Компилировать через ccache, если он установлен" ON)

# ccache: только если запускатель не задан снаружи (-DCMAKE_CXX_COMPILER_LAUNCHER=...).
if(HARMONY_USE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(HARMONY_CCACHE ccache)
    if(HARMONY_CCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER "${HARMONY_CCACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${HARMONY_CCACHE}")
    endif()
endif()

# Стандартные каталоги установки (bin, share…) для cmake --install и пакетов.
include(GNUInstallDirs)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/harmony" COMPONENT harmony)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)
include(RemoteProfiles)
include(RemoteCatalog)

# --- Модули ----------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(third_party/libconcord)
add_subdirectory(transport)

if(HARMONY_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(HARMONY_BUILD_APP)
    add_subdirectory(app)
endif()

if(HARMONY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HARMONY_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HARMONY_FUZZ требует Clang (libFuzzer), а компилятор: ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_subdirectory(fuzz)
endif()
