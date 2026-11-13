# Общий каталог моделей для core (без Qt) и ресурсов приложения.
file(GLOB remoteProfiles CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/app/resources/remotes/*/profile.json")
set(defaultProfile "${PROJECT_SOURCE_DIR}/app/resources/remotes/900/profile.json")
if(NOT EXISTS "${defaultProfile}")
    message(FATAL_ERROR "Default remote profile 900 is missing")
endif()
list(REMOVE_ITEM remoteProfiles "${defaultProfile}")
list(PREPEND remoteProfiles "${defaultProfile}")
set(remoteProfileEntries "")
# Общие данные экранов для всех моделей.
set(HARMONY_REMOTE_FILES
    "${PROJECT_SOURCE_DIR}/app/resources/layouts.json"
)
# Картинки для сборки экранов (screens.json моделей), общие для всех пультов.
file(GLOB_RECURSE harmonySprites CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/app/resources/sprites/*.png" "${PROJECT_SOURCE_DIR}/app/resources/sprites/*.svg")
list(APPEND HARMONY_REMOTE_FILES ${harmonySprites})
# новый remotes/<id>/screens.json подхватывается без ручного перезапуска CMake
file(GLOB harmonyModelScreens CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/app/resources/remotes/*/screens.json")
foreach(profileFile IN LISTS remoteProfiles)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${profileFile}")
    file(READ "${profileFile}" profileJson)
    string(JSON id GET "${profileJson}" id)
    string(JSON name GET "${profileJson}" name)
    string(JSON image GET "${profileJson}" image)
    get_filename_component(profileDir "${profileFile}" DIRECTORY)
    get_filename_component(directoryId "${profileDir}" NAME)
    if(NOT id MATCHES "^[a-z0-9]+$" OR NOT id STREQUAL directoryId OR
       NOT name MATCHES "^[A-Za-z0-9 .-]+$" OR NOT image STREQUAL "photo.png")
        message(FATAL_ERROR "Invalid remote profile: ${profileFile}")
    endif()
    string(JSON driver GET "${profileJson}" driver)
    string(JSON configFormat GET "${profileJson}" configFormat)
    if(NOT driver MATCHES "^(none|hid|harmony900)$" OR NOT configFormat MATCHES "^(unsupported|harmony900)$")
        message(FATAL_ERROR "Unknown driver/configFormat: ${profileFile}")
    endif()
    set(writable false)
    if(driver STREQUAL "harmony900" AND configFormat STREQUAL "harmony900")
        set(writable true)
    endif()
    string(APPEND remoteProfileEntries
        "    RemoteProfile{\"${id}\", \"${name}\", \":/res/remotes/${id}/${image}\", ${writable}, \"${driver}\", \"${configFormat}\"},\n")
    list(APPEND HARMONY_REMOTE_FILES "${profileFile}" "${profileDir}/${image}" "${profileDir}/zones.json")
    # экраны модели из картинок resources/sprites (необязательно): раскладки с "compose"
    if(EXISTS "${profileDir}/screens.json")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${profileDir}/screens.json")
        list(APPEND HARMONY_REMOTE_FILES "${profileDir}/screens.json")
    endif()
endforeach()
if(NOT remoteProfiles)
    message(FATAL_ERROR "No remote profiles found")
endif()
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/core/generated/harmony/core/remoteProfiles.inc"
    CONTENT "@remoteProfileEntries@" @ONLY)
