# transport — связь с пультом.
#
# Harmony 900 подключается к ПК как USB-Ethernet и доступен по адресу 169.254.1.2:
# конфигурация читается и пишется командами по TCP (порт 3074).

add_library(harmony_transport
    src/concordTransfer.cpp
    src/harmony900Link.cpp
    src/remoteServices.cpp
    src/tcpChannel.cpp
    src/usbnet.cpp
    src/harmonyHidReader.cpp
    src/hidChannel.cpp
)
add_library(harmony::transport ALIAS harmony_transport)

target_include_directories(harmony_transport
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(harmony_transport PUBLIC harmony::core)
target_link_libraries(harmony_transport PRIVATE hidapi::hidapi)
target_link_libraries(harmony_transport PRIVATE harmony_concord harmony_zip pugixml::pugixml)
if(WIN32)
    target_link_libraries(harmony_transport PRIVATE ws2_32)
endif()

target_compile_features(harmony_transport PUBLIC cxx_std_23)
harmony_set_warnings(harmony_transport)
