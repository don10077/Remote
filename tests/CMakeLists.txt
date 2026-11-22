# Unit-тесты ядра (GoogleTest). Запуск: ctest --preset debug

include(GoogleTest)

add_executable(harmony_tests
    transport/concordTransferTest.cpp
    activityUpdateTest.cpp
    configAddTest.cpp
    configButtonsTest.cpp
    configCheckTest.cpp
    configDocumentTest.cpp
    configHubDocumentTest.cpp
    buttonCopyTest.cpp
    buttonIconsTest.cpp
    deviceComparisonTest.cpp
    notSentTest.cpp
    noProfileMatrixTest.cpp
    deviceReplacementTest.cpp
    deviceStatesTest.cpp
    irEncodeTest.cpp
    configEditTest.cpp
    configEzHexTest.cpp
    formats/containerTest.cpp
    formats/digestTest.cpp
    formats/ezhexTest.cpp
    formats/gspmTest.cpp
    formats/gspmFontsTest.cpp
    formats/gspmScreensTest.cpp
    formats/hubConfigTest.cpp
    formats/irTablesTest.cpp
    formats/remoteCaptureTest.cpp
    formats/ssirSequencesTest.cpp
    formats/userConfigurationTest.cpp
    errorTest.cpp
    fixturesTest.cpp
    remoteCatalogTest.cpp
    modelInfoTest.cpp
    ir/broadlinkRecordingTest.cpp
    ir/irProtocolTest.cpp
    ir/nativeProtocolTest.cpp
    irdb/irDatabaseTest.cpp
    irdb/signalIndexTest.cpp
    ir/irSignalTest.cpp
    keyNamesTest.cpp
    model/remoteConfigTest.cpp
    macroTest.cpp
    propertiesTest.cpp
    remotePreferencesTest.cpp
    transport/harmony900LinkTest.cpp
    transport/harmonyHidReaderTest.cpp
    transport/hubConfigReaderTest.cpp
    transport/remoteServicesTest.cpp
    cliFilesTest.cpp
    ../cli/src/cliFiles.cpp
    undo/undoStackTest.cpp
    undoEditsTest.cpp
)

# Помощники тестов (`dumpFiles.h`, `formatFixtures.h`) лежат в `tests/` — видны всем тестам.
target_include_directories(harmony_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Путь к тестовым данным (данные, которые лежат в git) и к настоящим дампам (не в git —
# тесты на них пропускаются без папки, правило 19).
target_compile_definitions(harmony_tests PRIVATE
    HARMONY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    HARMONY_DUMPS_DIR="${CMAKE_SOURCE_DIR}/dumps"
    HARMONY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# harmony::core — через harmony::transport (PUBLIC), без дубля на строке линковки.
target_link_libraries(harmony_tests PRIVATE
    harmony::transport
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB # irdbFixture.h пишет .gz: усечённый источник должен распознаваться как ошибка
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
        noProfileWidgetsTest.cpp
        assignmentGateTest.cpp
        ../app/src/assignmentGate.cpp
        ../app/src/assignmentGate.h
        zoneEditingTest.cpp
        zoneEditorDialogTest.cpp
        ../app/src/widgets/zoneEditorDialog.cpp
        ../app/src/widgets/zoneEditorDialog.h
        ../app/src/widgets/zoneEditorProperties.cpp
        ../app/src/widgets/zoneEditorRemoteFile.cpp
        ../app/src/widgets/zoneEditorScreens.cpp
        ../app/src/widgets/zoneEditorShared.h
        ../app/src/widgets/zoneEditorZones.cpp
        ../app/src/widgets/zoneCanvas.cpp
        ../app/src/widgets/zoneCanvas.h
        ../app/src/i18n.cpp
        ../app/src/remoteResources.cpp
        ../app/src/zoneEditing.cpp
        ../app/src/modern/remotePanel.cpp
        ../app/src/modern/remotePanel.h
        ../app/src/modern/modernShell.cpp
        ../app/src/modern/modernShell.h
        ../app/src/icons.cpp
        ../app/src/modern/remoteView.cpp
        ../app/src/modern/remoteView.h
        ../app/src/modern/remoteViewInput.cpp
        ../app/src/modern/remoteViewPaint.cpp
        ../app/src/modern/remoteViewShared.h
        ../app/src/widgets/commandDialog.cpp
        ../app/src/widgets/commandDialog.h
        ../app/src/labels.cpp
    )
    set_target_properties(harmony_gui_tests PROPERTIES AUTOMOC ON)
    # Те же пути, что у harmony_tests: фикстуры в git и настоящие дампы (тесты на них пропускаются).
    target_compile_definitions(harmony_gui_tests PRIVATE
        HARMONY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        HARMONY_DUMPS_DIR="${CMAKE_SOURCE_DIR}/dumps"
        HARMONY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    file(GLOB guiActivityIcons CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/app/resources/activity_icons/*.png"
         "${PROJECT_SOURCE_DIR}/app/resources/theme/*")
    qt_add_resources(harmony_gui_tests "remote_test_resources"
        PREFIX "/res"
        BASE "${PROJECT_SOURCE_DIR}/app/resources"
        FILES ${HARMONY_REMOTE_FILES} ${guiActivityIcons} "${PROJECT_SOURCE_DIR}/app/resources/i18n/harmony_ru.ts"
    )
    target_link_libraries(harmony_gui_tests PRIVATE harmony::transport Qt6::Widgets Qt6::Svg GTest::gtest)
    harmony_set_warnings(harmony_gui_tests)
    add_test(NAME Gui.CommandDialog COMMAND harmony_gui_tests)
    set_tests_properties(Gui.CommandDialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900)
endif()
