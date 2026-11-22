# Общий каталог моделей для core (без Qt) и ресурсов приложения.
# Каждая модель описана одним файлом remotes/<id>/remote.json (схема — remotes/SCHEMA.md).
file(GLOB remoteProfiles CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/app/resources/remotes/*/remote.json")
set(defaultProfile "${PROJECT_SOURCE_DIR}/app/resources/remotes/900/remote.json")
if(NOT EXISTS "${defaultProfile}")
    message(FATAL_ERROR "Default remote profile 900 is missing")
endif()
list(REMOVE_ITEM remoteProfiles "${defaultProfile}")
list(PREPEND remoteProfiles "${defaultProfile}")
set(remoteProfileEntries "")
set(HARMONY_REMOTE_FILES "")
# Картинки экранов (спрайты), общие для всех пультов.
file(GLOB_RECURSE harmonySprites CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/app/resources/sprites/*.png" "${PROJECT_SOURCE_DIR}/app/resources/sprites/*.svg")
list(APPEND HARMONY_REMOTE_FILES ${harmonySprites})
foreach(profileFile IN LISTS remoteProfiles)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${profileFile}")
    file(READ "${profileFile}" profileJson)
    string(JSON id GET "${profileJson}" id)
    string(JSON name GET "${profileJson}" name)
    string(JSON image GET "${profileJson}" hardware image)
    get_filename_component(profileDir "${profileFile}" DIRECTORY)
    get_filename_component(directoryId "${profileDir}" NAME)
    if(NOT id MATCHES "^[a-z0-9]+$" OR NOT id STREQUAL directoryId OR
       NOT name MATCHES "^[A-Za-z0-9 .-]+$" OR NOT image STREQUAL "photo.png")
        message(FATAL_ERROR "Invalid remote profile: ${profileFile}")
    endif()
    # Имена — те же, что kLinkNames в core/include/harmony/core/remoteCatalog.h.
    # Поля нет или оно пустое: способ связи берётся из каталога модели.
    string(JSON connectionType ERROR_VARIABLE jsonError TYPE "${profileJson}" connection)
    set(connectionDriver "")
    if(jsonError STREQUAL "NOTFOUND" AND connectionType STREQUAL "OBJECT")
        string(JSON driverType ERROR_VARIABLE driverError TYPE "${profileJson}" connection driver)
        if(driverType STREQUAL "STRING")
            string(JSON connectionDriver GET "${profileJson}" connection driver)
        endif()
    endif()
    if(NOT connectionDriver MATCHES "^$|^(none|usb-net|usb-hid|mh|hub)$")
        message(FATAL_ERROR "Unknown connection driver: ${profileFile}")
    endif()
    string(APPEND remoteProfileEntries
        "    RemoteProfile{\"${id}\", \"${name}\", \":/res/remotes/${id}/${image}\", \"${connectionDriver}\"},\n")
    list(APPEND HARMONY_REMOTE_FILES "${profileFile}" "${profileDir}/${image}")
endforeach()
if(NOT remoteProfiles)
    message(FATAL_ERROR "No remote profiles found")
endif()
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/core/generated/harmony/core/remoteProfiles.inc"
    CONTENT "@remoteProfileEntries@" @ONLY)
