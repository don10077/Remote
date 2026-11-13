# Внешние зависимости проекта.
#
# Всё скачивается через FetchContent в каталог сборки (build/<preset>/_deps),
# поэтому дерево исходников остаётся чистым. Версии закреплены тегами.
#
# FIND_PACKAGE_ARGS: если библиотека уже установлена в системе
# (например, пакет из MSYS2 или дистрибутива Linux), берётся она,
# а скачивание пропускается.

include(FetchContent)

# Версии закреплены тегами, поэтому при повторной настройке не ходим в git за обновлениями
# (шаг update пропускается; смена тега в этом файле всё равно скачает зависимость заново).
# Вернуть проверку: -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF.
if(NOT DEFINED FETCHCONTENT_UPDATES_DISCONNECTED)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
endif()

# hidapi: архив релиза с проверкой sha256 вместо git, если хеш известен.
# TODO: вписать sha256 архива hidapi-0.14.0.tar.gz (см. URL ниже), тогда git не нужен.
set(HARMONY_HIDAPI_SHA256 "" CACHE STRING "sha256 архива hidapi-0.14.0.tar.gz; пусто — брать из git по тегу")

# USB HID для встроенного чтения 600/650/665/700, без внешней программы.
# Локальная область: статическая сборка HIDAPI не меняет тип остальных библиотек.
function(harmony_add_hidapi)
    # HIDAPI 0.14.0 объявляет CMake 3.1.3; CMake 4 удалил совместимость < 3.5
    # и предупреждает о версиях < 3.10. Поднимаем минимум политик до 3.10 только
    # в области этой зависимости, без правки _deps.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND
       (NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS "3.10"))
        set(CMAKE_POLICY_VERSION_MINIMUM "3.10")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(HIDAPI_WITH_LIBUSB OFF)
    set(HIDAPI_WITH_HIDRAW ON)
    set(HIDAPI_INSTALL_TARGETS OFF)
    if(HARMONY_HIDAPI_SHA256)
        FetchContent_Declare(hidapi
            URL https://github.com/libusb/hidapi/archive/refs/tags/hidapi-0.14.0.tar.gz
            URL_HASH SHA256=${HARMONY_HIDAPI_SHA256}
        )
    else()
        FetchContent_Declare(hidapi
            GIT_REPOSITORY https://github.com/libusb/hidapi.git
            GIT_TAG hidapi-0.14.0
            GIT_SHALLOW TRUE
        )
    endif()
    FetchContent_MakeAvailable(hidapi)
endfunction()
harmony_add_hidapi()

# --- GoogleTest (только для тестов) -----------------------------------------
if(HARMONY_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )
    # Не устанавливать gtest вместе с проектом и не собирать gmock-примеры.
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# --- pugixml: разбор UserConfiguration.xml и ActionLists.xml ---------------
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES pugixml
)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pugixml)

# --- zlib: сжатие файлов внутри .EZHex (ZIP) ---------------------------------
# В MSYS2 и дистрибутивах Linux zlib обычно уже есть (его ставит и Qt).
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES ZLIB
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
# Собранный из исходников zlib не объявляет ZLIB::ZLIB — даём то же имя, что у find_package.
if(NOT TARGET ZLIB::ZLIB AND TARGET zlibstatic)
    # SYSTEM: предупреждения из макросов zlib.h не должны ломать сборку с -Werror.
    target_include_directories(zlibstatic SYSTEM INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# --- nlohmann/json: база ИК-кодов Logitech (AllProtocols.json, записи устройств) ---
# Только заголовки. SYSTEM — предупреждения из чужого кода не ломают сборку с -Werror.
FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    SYSTEM
    FIND_PACKAGE_ARGS NAMES nlohmann_json
)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)
