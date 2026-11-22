# Pinned local Concordance snapshot, compiled into Harmony; no external executable.
add_library(harmony_zip INTERFACE)
if(APPLE)
    # Build both slices for the universal application; Homebrew libraries are native-only.
    function(harmony_build_zip)
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF)
        foreach(option ENABLE_COMMONCRYPTO ENABLE_GNUTLS ENABLE_OPENSSL ENABLE_WINDOWS_CRYPTO
                       ENABLE_BZIP2 ENABLE_LZMA ENABLE_ZSTD BUILD_TOOLS BUILD_REGRESS
                       BUILD_OSSFUZZ BUILD_EXAMPLES BUILD_DOC LIBZIP_DO_INSTALL)
            set(${option} OFF)
        endforeach()
        FetchContent_Declare(harmony_libzip
            GIT_REPOSITORY https://github.com/nih-at/libzip.git
            GIT_TAG v1.12.1 GIT_SHALLOW TRUE
            # SYSTEM — как у nlohmann_json и zlib: чужие заголовки не должны давать
            # диагностику в наших единицах перевода. Без этого Apple Clang с -Werror
            # падает на `-Wnullability-extension` в zip.h (прогон CI 121923762634):
            # `_Nullable` и `_Nonnull` — расширение Clang, а предупреждение приходит
            # из чужого заголовка, который мы включаем.
            SYSTEM)
        FetchContent_MakeAvailable(harmony_libzip)
        install(FILES "${harmony_libzip_SOURCE_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/harmony/libzip" COMPONENT harmony)
    endfunction()
    harmony_build_zip()
    target_link_libraries(harmony_zip INTERFACE libzip::zip)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CONCORD_ZIP REQUIRED IMPORTED_TARGET GLOBAL libzip)
    # Тот же смысл, что и в ветке выше: чужие заголовки — системные, поэтому diagnostics
    # из `zip.h` не становятся ошибками при HARMONY_WARNINGS_AS_ERRORS=ON.
    set_target_properties(PkgConfig::CONCORD_ZIP PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${CONCORD_ZIP_INCLUDE_DIRS}")
    target_link_libraries(harmony_zip INTERFACE PkgConfig::CONCORD_ZIP)
endif()
add_library(harmony_concord STATIC
    remote.cpp remote_z.cpp remote_mh.cpp libconcord.cpp binaryfile.cpp
    operationfile.cpp web.cpp usblan.cpp libhidapi.cpp
    remote_z_learn/data.cpp remote_z_learn/base.cpp
    remote_z_learn/single.cpp remote_z_learn/stream.cpp)
set_target_properties(harmony_concord PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS ON)
target_compile_definitions(harmony_concord PRIVATE WANT_HIDAPI=1)
# Clang (и AppleClang) ругается на смесь override и его отсутствия в чужих классах; код не правим.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(harmony_concord PRIVATE -Wno-inconsistent-missing-override)
endif()
target_include_directories(harmony_concord SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(harmony_concord PRIVATE hidapi::hidapi harmony_zip)
if(WIN32)
    target_link_libraries(harmony_concord PRIVATE wsock32)
endif()
install(FILES COPYING NOTICE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/harmony/libconcord" COMPONENT harmony)
