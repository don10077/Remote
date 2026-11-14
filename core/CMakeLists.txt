# core — ядро редактора: модель конфигурации, форматы файлов, undo/redo.
# Не зависит от Qt и от GUI: его можно использовать из app, cli и тестов.

# Заголовок с версией генерируется из project(VERSION ...).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/harmony/core/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/harmony/core/version.h"
    @ONLY
)

add_library(harmony_core
    src/remoteCatalog.cpp
    src/actionLists.cpp
    src/activityDraft.cpp
    src/buttonIcons.cpp
    src/configDocument.cpp
    src/harmony900Backend.cpp
    src/configDocumentAdd.cpp
    src/configDocumentButtons.cpp
    src/configDocumentCheck.cpp
    src/configDocumentCommands.cpp
    src/configDocumentDatabase.cpp
    src/configDocumentEdit.cpp
    src/configDocumentPackage.cpp
    src/configDocumentProperties.cpp
    src/configDocumentMacros.cpp
    src/configDocumentNotSent.cpp
    src/configDocumentReplace.cpp
    src/configDocumentStates.cpp
    src/configDiff.cpp
    src/formats/ezhex.cpp
    src/formats/gspm.cpp
    src/formats/gspmFonts.cpp
    src/formats/gspmScreens.cpp
    src/formats/hubConfig.cpp
    src/formats/irTables.cpp
    src/formats/ssirSequences.cpp
    src/formats/userConfiguration.cpp
    src/formats/zipArchive.cpp
    src/ir/irDecode.cpp
    src/ir/irEncode.cpp
    src/ir/carrierVm.cpp
    src/ir/irProtocol.cpp
    src/ir/nativeProtocol.cpp
    src/ir/irSignal.cpp
    src/irdb/irDatabase.cpp
    src/irdb/modelFinder.cpp
    src/irdb/signalIndex.cpp
    src/irdb/protocolLibrary.cpp
    src/model/remoteConfig.cpp
    src/propertyCatalog.cpp
    src/remotePreferences.cpp
    src/undo/documentEdit.cpp
    src/undo/undoStack.cpp
    src/xmlDom.cpp
)
add_library(harmony::core ALIAS harmony_core)

target_include_directories(harmony_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# pugixml, zlib и nlohmann/json нужны только внутри ядра: в публичных заголовках их типов нет.
target_link_libraries(harmony_core PRIVATE pugixml::pugixml ZLIB::ZLIB nlohmann_json::nlohmann_json)

target_compile_features(harmony_core PUBLIC cxx_std_23)
harmony_set_warnings(harmony_core)
