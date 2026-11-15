# cli — консольная утилита: проверка, дамп и резервное копирование конфигурации.

add_executable(harmony_cli
    src/main.cpp
    src/cliCommon.cpp
    src/cliFiles.cpp
    src/fileCommands.cpp
    src/hubCommands.cpp
    src/irdbCommands.cpp
    src/remoteCommands.cpp
    src/transferCommands.cpp)
set_target_properties(harmony_cli PROPERTIES OUTPUT_NAME "harmony-cli")
# harmony::core — через harmony::transport (PUBLIC), без дубля на строке линковки.
target_link_libraries(harmony_cli PRIVATE harmony::transport)
harmony_set_warnings(harmony_cli)

# CommandLineToArgvW (Unicode-аргументы) живёт в shell32.
if(WIN32)
    target_link_libraries(harmony_cli PRIVATE shell32)
endif()

# В пакетах утилита лежит рядом с приложением (на macOS её кладёт в бандл tools/make-macos.sh).
if(NOT APPLE)
    install(TARGETS harmony_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT harmony)
endif()
