# Unit-тесты ядра (GoogleTest). Запуск: ctest --preset debug

include(GoogleTest)

add_executable(harmony_tests
    transport/concordTransferTest.cpp
    activityUpdateTest.cpp
    configAddTest.cpp
    configButtonsTest.cpp
    configCheckTest.cpp
    configDocumentTest.cpp
    buttonCopyTest.cpp
    buttonIconsTest.cpp
    deviceComparisonTest.cpp
    notSentTest.cpp
    deviceReplacementTest.cpp
    deviceStatesTest.cpp
    irEncodeTest.cpp
    configEditTest.cpp
    configEzHexTest.cpp
    formats/ezhexTest.cpp
    formats/irTablesTest.cpp
    formats/userConfigurationTest.cpp
    errorTest.cpp
    ir/broadlinkRecordingTest.cpp
    ir/irProtocolTest.cpp
    ir/nativeProtocolTest.cpp
    irdb/irDatabaseTest.cpp
    irdb/signalIndexTest.cpp
    ir/irSignalTest.cpp
    model/remoteConfigTest.cpp
    macroTest.cpp
    propertiesTest.cpp
    remotePreferencesTest.cpp
    transport/harmony900LinkTest.cpp
    transport/harmonyHidReaderTest.cpp
    transport/remoteServicesTest.cpp
    undo/undoStackTest.cpp
    undoEditsTest.cpp
)

# Путь к тестовым данным (реальная обезличенная конфигурация пульта).
target_compile_definitions(harmony_tests PRIVATE
    HARMONY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    HARMONY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(harmony_tests PRIVATE
    harmony::core
    harmony::transport
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
harmony_set_warnings(harmony_tests)

# Каждый TEST() регистрируется в ctest отдельно.
gtest_discover_tests(harmony_tests)

if(HARMONY_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)
    add_executable(harmony_gui_tests
        protocolCommandDialogTest.cpp
        ../app/src/widgets/protocolCommandDialog.cpp
        ../app/src/widgets/protocolCommandDialog.h
        ../app/src/widgets/learnWizardDialog.cpp
        ../app/src/widgets/learnWizardDialog.h
        ../app/src/remoteSession.cpp
        ../app/src/remoteSession.h
        ../app/src/theme.cpp
        ../app/src/appSettings.cpp
        ../app/src/journal.cpp
        commandDialogTest.cpp
        remoteViewTest.cpp
        remoteResourcesTest.cpp
        zoneEditingTest.cpp
        zoneEditorDialogTest.cpp
        ../app/src/widgets/zoneEditorDialog.cpp
        ../app/src/widgets/zoneEditorDialog.h
        ../app/src/widgets/zoneCanvas.cpp
        ../app/src/widgets/zoneCanvas.h
        ../app/src/i18n.cpp
        ../app/src/remoteResources.cpp
        ../app/src/zoneEditing.cpp
        ../app/src/modern/remotePanel.cpp
        ../app/src/modern/remotePanel.h
        ../app/src/icons.cpp
        ../app/src/modern/remoteView.cpp
        ../app/src/modern/remoteView.h
        ../app/src/widgets/commandDialog.cpp
        ../app/src/widgets/commandDialog.h
        ../app/src/labels.cpp
    )
    set_target_properties(harmony_gui_tests PROPERTIES AUTOMOC ON)
    file(GLOB guiActivityIcons CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/app/resources/activity_icons/*.png"
         "${PROJECT_SOURCE_DIR}/app/resources/theme/*")
    qt_add_resources(harmony_gui_tests "remote_test_resources"
        PREFIX "/res"
        BASE "${PROJECT_SOURCE_DIR}/app/resources"
        FILES ${HARMONY_REMOTE_FILES} ${guiActivityIcons} "${PROJECT_SOURCE_DIR}/app/resources/i18n/harmony_ru.ts"
    )
    target_link_libraries(harmony_gui_tests PRIVATE harmony::core harmony::transport Qt6::Widgets Qt6::Svg GTest::gtest)
    harmony_set_warnings(harmony_gui_tests)
    add_test(NAME Gui.CommandDialog COMMAND harmony_gui_tests)
    set_tests_properties(Gui.CommandDialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
