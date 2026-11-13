# app — графическое приложение на Qt 6 Widgets.

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Svg)
qt_standard_project_setup()   # включает AUTOMOC/AUTOUIC и стандартные настройки Qt

qt_add_executable(harmony_app
    src/widgets/configTransferDialog.cpp
    src/widgets/configTransferDialog.h
    src/main.cpp
    src/appSettings.cpp
    src/appSettings.h
    src/i18n.cpp
    src/journal.cpp
    src/journal.h
    src/i18n.h
    src/mainWindow.cpp
    src/help.cpp
    src/help.h
    src/helpContent.cpp
    src/widgets/aboutDialog.cpp
    src/widgets/aboutDialog.h
    src/widgets/disclaimerDialog.cpp
    src/widgets/disclaimerDialog.h
    src/widgets/helpDialog.cpp
    src/widgets/helpDialog.h
    src/widgets/remoteProfileDialog.cpp
    src/widgets/remoteProfileDialog.h
    src/widgets/remoteSettingsDialog.cpp
    src/widgets/remoteSettingsDialog.h
    src/mainWindowRemote.cpp
    src/linuxUsbSetup.cpp
    src/linuxUsbSetup.h
    src/mainWindowClipboard.cpp
    src/mainWindowVersions.cpp
    src/mainWindow.h
    src/configLibrary.cpp
    src/configLibrary.h
    src/remoteSession.cpp
    src/remoteSession.h
    src/documentSource.cpp
    src/documentSource.h
    src/icons.cpp
    src/irDatabaseService.cpp
    src/irDatabaseService.h
    src/icons.h
    src/labels.cpp
    src/labels.h
    src/nameCheck.h
    src/theme.cpp
    src/theme.h
    src/widgets/appHeader.cpp
    src/widgets/activityDialog.cpp
    src/widgets/activityDialog.h
    src/widgets/addDeviceDialog.cpp
    src/widgets/addDeviceDialog.h
    src/versionStore.cpp
    src/versionStore.h
    src/widgets/quickSearch.cpp
    src/widgets/quickSearch.h
    src/widgets/versionsDialog.cpp
    src/widgets/versionsDialog.h
    src/widgets/compareConfigsDialog.cpp
    src/widgets/compareConfigsDialog.h
    src/widgets/compareDevicesDialog.cpp
    src/widgets/compareDevicesDialog.h
    src/widgets/copyButtonsDialog.cpp
    src/widgets/copyButtonsDialog.h
    src/widgets/deviceStatesDialog.cpp
    src/widgets/deviceStatesDialog.h
    src/widgets/journalDialog.cpp
    src/widgets/journalDialog.h
    src/widgets/learnWizardDialog.cpp
    src/widgets/learnWizardDialog.h
    src/widgets/propertiesDialog.cpp
    src/widgets/propertiesDialog.h
    src/widgets/protocolCommandDialog.cpp
    src/widgets/protocolCommandDialog.h
    src/widgets/replaceDeviceDialog.cpp
    src/widgets/replaceDeviceDialog.h
    src/widgets/checkDialog.cpp
    src/widgets/checkDialog.h
    src/widgets/writePreviewDialog.cpp
    src/widgets/writePreviewDialog.h
    src/widgets/findByRemoteDialog.cpp
    src/widgets/findByRemoteDialog.h
    src/widgets/appHeader.h
    src/widgets/buttonDialog.cpp
    src/widgets/buttonDialog.h
    src/widgets/buttonRef.h
    src/widgets/commandDialog.cpp
    src/widgets/commandDialog.h
    src/widgets/configView.cpp
    src/widgets/configView.h
    src/widgets/irDatabasePanel.cpp
    src/widgets/irDatabasePanel.h
    src/widgets/newConfigDialog.cpp
    src/widgets/newConfigDialog.h
    src/widgets/newDeviceDialog.cpp
    src/widgets/newDeviceDialog.h
    src/widgets/settingsDialog.cpp
    src/widgets/settingsDialog.h
    src/widgets/startDialog.cpp
    src/widgets/startDialog.h
    src/widgets/toggleSwitch.cpp
    src/widgets/toggleSwitch.h
    src/widgets/welcomePage.cpp
    src/widgets/welcomePage.h
    src/modern/activityIcon.h
    src/modern/modernShell.cpp
    src/modern/modernShell.h
    src/modern/remotePanel.cpp
    src/modern/remotePanel.h
    src/remoteResources.cpp
    src/remoteResources.h
    src/zoneEditing.cpp
    src/zoneEditing.h
    src/widgets/zoneCanvas.cpp
    src/widgets/zoneCanvas.h
    src/widgets/zoneEditorDialog.cpp
    src/widgets/zoneEditorDialog.h
    src/modern/remoteView.cpp
    src/modern/remoteView.h
)

# Ресурсы под префиксом ":/res/": тема, значки активностей, фото пульта, иконка окна.
file(GLOB activityIcons RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" resources/activity_icons/*.png)
# QR-код страницы пожертвований для окна «О программе»; без файла окно показывает только ссылку.
set(donateFiles)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/donate-qr.png")
    list(APPEND donateFiles resources/app/donate-qr.png)
endif()

qt_add_resources(harmony_app "res"
    PREFIX "/res"
    BASE "resources"
    FILES
        resources/theme/modern.qss
        resources/theme/harmony.qss
        resources/theme/arrow-down.png
        resources/theme/arrow-up.png
        resources/theme/arrow-up-dark.png
        resources/theme/arrow-down-dark.png
        resources/theme/check.png
        resources/theme/radio-dot.png
        ${HARMONY_REMOTE_FILES}
        resources/app/harmony-16.png
        resources/app/harmony-24.png
        resources/app/harmony-32.png
        resources/app/harmony-48.png
        resources/app/harmony-64.png
        resources/app/harmony-128.png
        resources/app/harmony-256.png
        ${donateFiles}
        resources/i18n/harmony_ru.ts
        resources/export/README.en.md
        resources/export/README.ru.md
        ${activityIcons}
)

# Встроенный пример — обезличенная конфигурация из тестов со всеми файлами пульта,
# доступна как ":/demo/<файл>". Файлы одни на тесты и приложение.
set(demoDir "${PROJECT_SOURCE_DIR}/tests/data/userconfig")
set(demoFiles)
foreach(demoFile UserConfiguration.xml ActionLists.xml IrProto.bin SsIr.bin)
    set_source_files_properties("${demoDir}/${demoFile}" PROPERTIES QT_RESOURCE_ALIAS "${demoFile}")
    list(APPEND demoFiles "${demoDir}/${demoFile}")
endforeach()
qt_add_resources(harmony_app "demo"
    PREFIX "/demo"
    FILES ${demoFiles}
)

# Linux: автонастройка связи с Harmony 900 по USB (правило udev + DHCP), доступна как
# ":/usbnet/<файл>". Программа копирует файлы во временную папку и ставит через pkexec:
# из AppImage root файлы прочитать не может.
if(UNIX AND NOT APPLE)
    set(usbnetDir "${PROJECT_SOURCE_DIR}/packaging/linux/usbnet")
    set(usbnetFiles)
    foreach(usbnetFile install-usbnet.sh harmony-usbnet.sh 99-harmony-usbnet.rules 70-harmony-hid.rules)
        set_source_files_properties("${usbnetDir}/${usbnetFile}" PROPERTIES QT_RESOURCE_ALIAS "${usbnetFile}")
        list(APPEND usbnetFiles "${usbnetDir}/${usbnetFile}")
    endforeach()
    qt_add_resources(harmony_app "usbnet"
        PREFIX "/usbnet"
        FILES ${usbnetFiles}
    )
endif()

# Значок exe в Проводнике Windows. Генерируется tools/makeAppIcon.py.
if(WIN32)
    target_sources(harmony_app PRIVATE resources/app/harmony.rc)
endif()

# Значок бандла macOS: harmony.icns собирается iconutil из тех же PNG при сборке.
if(APPLE)
    set(iconset "${CMAKE_CURRENT_BINARY_DIR}/harmony.iconset")
    set(icns "${CMAKE_CURRENT_BINARY_DIR}/harmony.icns")
    set(iconsetCopy)
    # Имена файлов набора требует iconutil: icon_<N>x<N>.png и icon_<N>x<N>@2x.png.
    foreach(pair "16;16" "32;16@2x" "32;32" "64;32@2x" "128;128" "256;128@2x" "256;256" "512;256@2x" "512;512")
        list(GET pair 0 png)
        list(GET pair 1 name)
        list(APPEND iconsetCopy COMMAND "${CMAKE_COMMAND}" -E copy
             "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/harmony-${png}.png" "${iconset}/icon_${name}.png")
    endforeach()
    add_custom_command(OUTPUT "${icns}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${iconset}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${iconset}"
        ${iconsetCopy}
        COMMAND iconutil -c icns -o "${icns}" "${iconset}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/harmony-512.png"
        COMMENT "Значок приложения для macOS (harmony.icns)"
        VERBATIM
    )
    set_source_files_properties("${icns}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(harmony_app PRIVATE "${icns}")
endif()

set_target_properties(harmony_app PROPERTIES
    OUTPUT_NAME "harmony"
    WIN32_EXECUTABLE ON      # без консольного окна под Windows
    # macOS: бандл Harmony.app с описанием для Finder.
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Harmony"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.don10077.harmony"
    MACOSX_BUNDLE_ICON_FILE "harmony.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Harmony"
)
if(APPLE)
    # В Finder бандл называется Harmony.app, а не harmony.app.
    set_target_properties(harmony_app PROPERTIES OUTPUT_NAME "Harmony")
endif()

target_link_libraries(harmony_app PRIVATE
    harmony::core
    harmony::transport
    Qt6::Widgets
    Qt6::Svg      # SVG-картинки экранов из resources/sprites
    Qt6::Network   # скачивание базы ИК-кодов
)
harmony_set_warnings(harmony_app)

# --- Установка (cmake --install --component harmony): основа AppImage и бандла macOS ---
# Компонент harmony отделяет наши файлы от правил установки зависимостей из
# FetchContent (zlib, например, ставит себя по абсолютным путям в /usr/local).
install(TARGETS harmony_app
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT harmony
    BUNDLE  DESTINATION "."                       COMPONENT harmony
)

# Linux: ярлык в меню приложений и значки всех размеров (тема hicolor).
if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/harmony.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
            COMPONENT harmony)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES "resources/app/harmony-${size}.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
                RENAME "harmony.png"
                COMPONENT harmony)
    endforeach()
endif()
