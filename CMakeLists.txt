cmake_minimum_required(VERSION 3.22)

project(
    CAuth
    VERSION 0.6.0
    DESCRIPTION "Native Steam authentication, depot, and cloud toolkit"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(CAUTH_BUILD_CLI "Build the CAuth command line tool" ON)
option(CAUTH_BUILD_TESTS "Build CAuth tests" ON)
option(CAUTH_BUILD_FFI "Build the CAuth C ABI shared library" ON)
set(CAUTH_ZLIB_ROOT "" CACHE PATH "Optional zlib root for compressed Steam CMsgMulti support")
set(CAUTH_ZSTD_ROOT "" CACHE PATH "Optional zstd root for VZstd depot chunk support")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CAUTH_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CAUTH_GENERATED_INCLUDE_DIR}/core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cauth_version_generated.hpp.in"
    "${CAUTH_GENERATED_INCLUDE_DIR}/core/version_generated.hpp"
    @ONLY
)

set(CAUTH_CORE_SOURCES
    third_party/lzma-sdk/LzmaDec.c
    src/application/cauth_application.cpp
    src/core/session/auth_session.cpp
    src/core/session/auth_session_codec.cpp
    src/core/auth/jwt.cpp
    src/core/crypto/aes.cpp
    src/core/encoding/base64url.cpp
    src/core/hash/sha1.cpp
    src/core/platform/endpoint_route_cache.cpp
    src/core/platform/file_write.cpp
    src/core/platform/http_client.cpp
    src/core/platform/password_crypto.cpp
    src/core/platform/session_repository_factory.cpp
    src/core/platform/websocket_client.cpp
    src/core/transfer/transfer_checkpoint.cpp
    src/core/transfer/transfer_resume.cpp
    src/core/runtime/android/bridge.cpp
    src/core/runtime/android/secure_storage_bridge.cpp
    src/core/runtime/session/android_session_repository.cpp
    src/core/runtime/session/memory_session_repository.cpp
    src/core/runtime/windows/windows_session_repository.cpp
    src/core/version.cpp
)

set(CAUTH_STEAM_AUTH_SOURCES
    src/steam/auth/cm_authentication_transport.cpp
    src/steam/auth/device_name.cpp
    src/steam/auth/placeholder_steam_authenticator.cpp
    src/steam/auth/platform_auth_runtime.cpp
    src/steam/auth/steam_auth_provider.cpp
    src/steam/auth/steam_auth_application.cpp
    src/steam/auth/steam_auth_cm_application.cpp
    src/steam/auth/steam_login_service.cpp
    src/steam/auth/steam_mobile_app_login.cpp
    src/steam/auth/steam_network_authenticator.cpp
    src/steam/auth/steam_password_encryptor.cpp
    src/steam/auth/steam_web_cookie_service.cpp
    src/steam/auth/steam_web_api_auth_transport.cpp
    src/steam/auth/unimplemented_steam_auth_transport.cpp
    src/steam/cm/cm_client_hello.cpp
    src/steam/cm/cm_heartbeat.cpp
    src/steam/cm/cm_logon.cpp
    src/steam/cm/cm_message.cpp
    src/steam/cm/cm_service_method.cpp
    src/steam/cm/cm_session.cpp
    src/steam/cm/steam_cm_connector.cpp
    src/steam/cm/steam_directory.cpp
    src/steam/cm/websocket_transport.cpp
)

set(CAUTH_STEAM_DEPOT_SOURCES
    src/steam/depot/app_info.cpp
    src/steam/depot/depot_cm_client.cpp
    src/steam/depot/cdn_directory.cpp
    src/steam/depot/depot_chunk.cpp
    src/steam/depot/depot_file.cpp
    src/steam/depot/depot_key.cpp
    src/steam/depot/depot_manifest.cpp
    src/steam/depot/depot_preflight.cpp
    src/steam/depot/depot_resolver.cpp
    src/steam/depot/manifest_downloader.cpp
    src/steam/depot/manifest_request_code.cpp
    src/steam/depot/pics.cpp
    src/steam/depot/steam_depot_application.cpp
)

set(CAUTH_STEAM_CLOUD_SOURCES
    src/steam/cloud/steam_cloud_cm_service.cpp
    src/steam/cloud/steam_cloud_service.cpp
    src/steam/cloud/steam_cloud_upload_service.cpp
    src/steam/cloud/steam_cloud_application.cpp
)

add_library(cauth_core ${CAUTH_CORE_SOURCES})
add_library(cauth_steam_auth ${CAUTH_STEAM_AUTH_SOURCES})
add_library(cauth_steam_depot ${CAUTH_STEAM_DEPOT_SOURCES})
add_library(cauth_steam_cloud ${CAUTH_STEAM_CLOUD_SOURCES})
add_library(cauth_steam INTERFACE)

add_library(cauth::core ALIAS cauth_core)
add_library(cauth::steam_auth ALIAS cauth_steam_auth)
add_library(cauth::steam_depot ALIAS cauth_steam_depot)
add_library(cauth::steam_cloud ALIAS cauth_steam_cloud)
add_library(cauth::steam ALIAS cauth_steam)

set_target_properties(cauth_core PROPERTIES EXPORT_NAME core)
set_target_properties(cauth_steam_auth PROPERTIES EXPORT_NAME steam_auth)
set_target_properties(cauth_steam_depot PROPERTIES EXPORT_NAME steam_depot)
set_target_properties(cauth_steam_cloud PROPERTIES EXPORT_NAME steam_cloud)
set_target_properties(cauth_steam PROPERTIES EXPORT_NAME steam)

set(CAUTH_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CAuth")
set(CAUTH_COMPONENT_CORE "Core")
set(CAUTH_COMPONENT_STEAM_AUTH "SteamAuth")
set(CAUTH_COMPONENT_STEAM_DEPOT "SteamDepot")
set(CAUTH_COMPONENT_STEAM_CLOUD "SteamCloud")
set(CAUTH_COMPONENT_CLI "Cli")

function(cauth_configure_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzma-sdk
    )
    target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()

cauth_configure_target(cauth_core)
cauth_configure_target(cauth_steam_auth)
cauth_configure_target(cauth_steam_depot)
cauth_configure_target(cauth_steam_cloud)

target_include_directories(cauth_core PRIVATE "${CAUTH_GENERATED_INCLUDE_DIR}")

target_link_libraries(cauth_steam_auth PUBLIC cauth::core)
target_link_libraries(cauth_steam_depot PUBLIC cauth::steam_auth)
target_link_libraries(cauth_steam_cloud PUBLIC cauth::steam_auth)
target_link_libraries(cauth_steam INTERFACE cauth::steam_auth cauth::steam_depot cauth::steam_cloud)

set(CAUTH_ZLIB_FOUND FALSE)
set(CAUTH_ZLIB_INCLUDE_DIR "")
set(CAUTH_ZLIB_LIBRARY "")
set(CAUTH_ZLIB_RUNTIME "")
set(CAUTH_ZLIB_BUNDLED_ROOT "")

if(CAUTH_ZLIB_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${CAUTH_ZLIB_ROOT}")
    set(ZLIB_ROOT "${CAUTH_ZLIB_ROOT}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib-1.3.1/zlib.h")
    set(CAUTH_ZLIB_BUNDLED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib-1.3.1")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib/zlib.h")
    set(CAUTH_ZLIB_BUNDLED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib")
endif()

if(CAUTH_ZLIB_BUNDLED_ROOT)
    set(CAUTH_ZLIB_INCLUDE_DIR "${CAUTH_ZLIB_BUNDLED_ROOT}")
    add_library(cauth_zlib_static STATIC
        "${CAUTH_ZLIB_BUNDLED_ROOT}/adler32.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/compress.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/crc32.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/deflate.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/inflate.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/inffast.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/inftrees.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/trees.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/uncompr.c"
        "${CAUTH_ZLIB_BUNDLED_ROOT}/zutil.c"
    )
    target_include_directories(cauth_zlib_static PRIVATE "${CAUTH_ZLIB_BUNDLED_ROOT}")
    set(CAUTH_ZLIB_LIBRARY cauth_zlib_static)
    set(CAUTH_ZLIB_FOUND TRUE)
else()
    find_package(ZLIB QUIET)

    if(ZLIB_FOUND)
        set(CAUTH_ZLIB_FOUND TRUE)
        if(DEFINED ZLIB_INCLUDE_DIRS)
            set(CAUTH_ZLIB_INCLUDE_DIR "${ZLIB_INCLUDE_DIRS}")
        elseif(DEFINED ZLIB_INCLUDE_DIR)
            set(CAUTH_ZLIB_INCLUDE_DIR "${ZLIB_INCLUDE_DIR}")
        endif()
    elseif(ANDROID AND EXISTS "${CMAKE_SYSROOT}/usr/include/zlib.h")
        set(CAUTH_ZLIB_INCLUDE_DIR "${CMAKE_SYSROOT}/usr/include")
        find_library(CAUTH_ZLIB_LIBRARY NAMES z)
        if(CAUTH_ZLIB_LIBRARY)
            set(CAUTH_ZLIB_FOUND TRUE)
        endif()
    else()
        find_path(CAUTH_ZLIB_INCLUDE_DIR
            NAMES zlib.h
            HINTS
                "${CAUTH_ZLIB_ROOT}/include"
                "$ENV{CAUTH_ZLIB_ROOT}/include"
                "C:/Program Files/Common Files/Reallusion/PostgreSQL/include"
        )
        find_library(CAUTH_ZLIB_LIBRARY
            NAMES zlib zlib1
            HINTS
                "${CAUTH_ZLIB_ROOT}/lib"
                "$ENV{CAUTH_ZLIB_ROOT}/lib"
                "C:/Program Files/Common Files/Reallusion/PostgreSQL/lib"
        )
        if(CAUTH_ZLIB_INCLUDE_DIR AND CAUTH_ZLIB_LIBRARY)
            set(CAUTH_ZLIB_FOUND TRUE)
        endif()
    endif()
endif()

find_file(CAUTH_ZLIB_RUNTIME
    NAMES zlib1.dll zlib.dll
    HINTS
        "${CAUTH_ZLIB_ROOT}/bin"
        "$ENV{CAUTH_ZLIB_ROOT}/bin"
        "C:/Program Files/Common Files/Reallusion/PostgreSQL/bin"
)

if(CAUTH_ZLIB_FOUND)
    target_compile_definitions(cauth_steam_auth PRIVATE CAUTH_HAS_ZLIB=1)
    target_compile_definitions(cauth_steam_depot PRIVATE CAUTH_HAS_ZLIB=1)
    target_compile_definitions(cauth_steam_cloud PRIVATE CAUTH_HAS_ZLIB=1)
    if(CAUTH_ZLIB_INCLUDE_DIR)
        target_include_directories(cauth_steam_auth PRIVATE "${CAUTH_ZLIB_INCLUDE_DIR}")
        target_include_directories(cauth_steam_depot PRIVATE "${CAUTH_ZLIB_INCLUDE_DIR}")
        target_include_directories(cauth_steam_cloud PRIVATE "${CAUTH_ZLIB_INCLUDE_DIR}")
    endif()
    if(CAUTH_ZLIB_LIBRARY STREQUAL "cauth_zlib_static")
        target_link_libraries(cauth_steam_auth PRIVATE cauth_zlib_static)
        target_link_libraries(cauth_steam_depot PRIVATE cauth_zlib_static)
        target_link_libraries(cauth_steam_cloud PRIVATE cauth_zlib_static)
    elseif(TARGET ZLIB::ZLIB)
        target_link_libraries(cauth_steam_auth PRIVATE ZLIB::ZLIB)
        target_link_libraries(cauth_steam_depot PRIVATE ZLIB::ZLIB)
        target_link_libraries(cauth_steam_cloud PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(cauth_steam_auth PRIVATE "${CAUTH_ZLIB_LIBRARY}")
        target_link_libraries(cauth_steam_depot PRIVATE "${CAUTH_ZLIB_LIBRARY}")
        target_link_libraries(cauth_steam_cloud PRIVATE "${CAUTH_ZLIB_LIBRARY}")
    endif()
    if(CAUTH_ZLIB_RUNTIME)
        configure_file("${CAUTH_ZLIB_RUNTIME}" "${CMAKE_BINARY_DIR}/zlib1.dll" COPYONLY)
    endif()
endif()

find_path(CAUTH_ZSTD_INCLUDE_DIR
    NAMES zstd.h
    HINTS
        "${CAUTH_ZSTD_ROOT}/include"
        "$ENV{CAUTH_ZSTD_ROOT}/include"
)
find_library(CAUTH_ZSTD_LIBRARY
    NAMES zstd libzstd libzstd_static
    HINTS
        "${CAUTH_ZSTD_ROOT}/lib"
        "${CAUTH_ZSTD_ROOT}/static"
        "$ENV{CAUTH_ZSTD_ROOT}/lib"
        "$ENV{CAUTH_ZSTD_ROOT}/static"
)
find_file(CAUTH_ZSTD_RUNTIME
    NAMES zstd.dll libzstd.dll
    HINTS
        "${CAUTH_ZSTD_ROOT}/bin"
        "$ENV{CAUTH_ZSTD_ROOT}/bin"
)

if(NOT CAUTH_ZSTD_INCLUDE_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib/zstd.h")
    set(CAUTH_ZSTD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib")
endif()

if(CAUTH_ZSTD_INCLUDE_DIR AND NOT CAUTH_ZSTD_LIBRARY AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib/decompress/zstd_decompress.c")
    add_library(cauth_zstd_static STATIC
        .deps/zstd-1.5.7/lib/common/debug.c
        .deps/zstd-1.5.7/lib/common/entropy_common.c
        .deps/zstd-1.5.7/lib/common/error_private.c
        .deps/zstd-1.5.7/lib/common/fse_decompress.c
        .deps/zstd-1.5.7/lib/common/pool.c
        .deps/zstd-1.5.7/lib/common/threading.c
        .deps/zstd-1.5.7/lib/common/xxhash.c
        .deps/zstd-1.5.7/lib/common/zstd_common.c
        .deps/zstd-1.5.7/lib/decompress/huf_decompress.c
        .deps/zstd-1.5.7/lib/decompress/zstd_ddict.c
        .deps/zstd-1.5.7/lib/decompress/zstd_decompress.c
        .deps/zstd-1.5.7/lib/decompress/zstd_decompress_block.c
    )
    target_include_directories(cauth_zstd_static
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib/common"
            "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zstd-1.5.7/lib/decompress"
    )
    target_compile_definitions(cauth_zstd_static PRIVATE ZSTD_DISABLE_ASM=1)
    set(CAUTH_ZSTD_LIBRARY cauth_zstd_static)
endif()

if(CAUTH_ZSTD_INCLUDE_DIR AND CAUTH_ZSTD_LIBRARY)
    target_compile_definitions(cauth_steam_depot PRIVATE CAUTH_HAS_ZSTD=1)
    target_include_directories(cauth_steam_depot PRIVATE "${CAUTH_ZSTD_INCLUDE_DIR}")
    if(CAUTH_ZSTD_LIBRARY STREQUAL "cauth_zstd_static")
        target_link_libraries(cauth_steam_depot PRIVATE cauth_zstd_static)
    else()
        target_link_libraries(cauth_steam_depot PRIVATE "${CAUTH_ZSTD_LIBRARY}")
    endif()
    if(CAUTH_ZSTD_RUNTIME)
        configure_file("${CAUTH_ZSTD_RUNTIME}" "${CMAKE_BINARY_DIR}/zstd.dll" COPYONLY)
    endif()
endif()

if(WIN32)
    target_link_libraries(cauth_core PRIVATE Bcrypt Crypt32 Winhttp)
    target_link_libraries(cauth_steam_auth PRIVATE Winhttp)
    target_link_libraries(cauth_steam_depot PRIVATE Winhttp)
    target_link_libraries(cauth_steam_cloud PRIVATE Winhttp)
endif()

if(MSVC)
    target_compile_options(cauth_core PRIVATE /W4 /permissive-)
    target_compile_options(cauth_steam_auth PRIVATE /W4 /permissive-)
    target_compile_options(cauth_steam_depot PRIVATE /W4 /permissive-)
    target_compile_options(cauth_steam_cloud PRIVATE /W4 /permissive-)
else()
    target_compile_options(cauth_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cauth_steam_auth PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cauth_steam_depot PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cauth_steam_cloud PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CAUTH_BUILD_FFI)
    add_library(cauth_core_ffi SHARED src/ffi/core.cpp)
    add_library(cauth::core_ffi ALIAS cauth_core_ffi)
    target_link_libraries(cauth_core_ffi PRIVATE cauth::core)
    target_include_directories(cauth_core_ffi
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(cauth_core_ffi PRIVATE CAUTH_BUILD_SHARED=1)
    set_target_properties(cauth_core_ffi PROPERTIES
        EXPORT_NAME core_ffi
        OUTPUT_NAME cauth_core_ffi
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    add_library(cauth_steam_auth_ffi SHARED src/ffi/steam_auth.cpp)
    add_library(cauth::steam_auth_ffi ALIAS cauth_steam_auth_ffi)
    target_link_libraries(cauth_steam_auth_ffi PRIVATE cauth_core_ffi cauth::steam_auth)
    target_include_directories(cauth_steam_auth_ffi
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(cauth_steam_auth_ffi PRIVATE CAUTH_BUILD_SHARED=1)
    set_target_properties(cauth_steam_auth_ffi PROPERTIES
        EXPORT_NAME steam_auth_ffi
        OUTPUT_NAME cauth_steam_auth_ffi
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    add_library(cauth_steam_depot_ffi SHARED src/ffi/steam_depot.cpp)
    add_library(cauth::steam_depot_ffi ALIAS cauth_steam_depot_ffi)
    target_link_libraries(cauth_steam_depot_ffi PRIVATE cauth_core_ffi cauth::steam_depot)
    target_include_directories(cauth_steam_depot_ffi
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(cauth_steam_depot_ffi PRIVATE CAUTH_BUILD_SHARED=1)
    set_target_properties(cauth_steam_depot_ffi PROPERTIES
        EXPORT_NAME steam_depot_ffi
        OUTPUT_NAME cauth_steam_depot_ffi
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    add_library(cauth_steam_cloud_ffi SHARED src/ffi/steam_cloud.cpp)
    add_library(cauth::steam_cloud_ffi ALIAS cauth_steam_cloud_ffi)
    target_link_libraries(cauth_steam_cloud_ffi PRIVATE cauth_core_ffi cauth::steam_cloud)
    target_include_directories(cauth_steam_cloud_ffi
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(cauth_steam_cloud_ffi PRIVATE CAUTH_BUILD_SHARED=1)
    set_target_properties(cauth_steam_cloud_ffi PROPERTIES
        EXPORT_NAME steam_cloud_ffi
        OUTPUT_NAME cauth_steam_cloud_ffi
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endif()

if(CAUTH_BUILD_CLI)
    add_executable(cauth
        src/cli/cli_support.cpp
        src/cli/main.cpp
        src/cli/steam_cli.cpp
        src/cli/steam_auth_cli.cpp
        src/cli/steam_depot_cli.cpp
        src/cli/steam_cloud_cli.cpp
    )
    target_link_libraries(cauth PRIVATE cauth::core cauth::steam_auth cauth::steam_depot cauth::steam_cloud)
    target_include_directories(cauth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(cauth PRIVATE /W4 /permissive-)
    else()
        target_compile_options(cauth PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(CAUTH_BUILD_TESTS)
    enable_testing()

    add_executable(cauth_core_tests tests/core_version_tests.cpp)
    if(CAUTH_BUILD_FFI)
        target_link_libraries(cauth_core_tests PRIVATE cauth::core_ffi cauth::core)
    else()
        target_link_libraries(cauth_core_tests PRIVATE cauth::core)
    endif()

    add_test(NAME cauth_core_tests COMMAND cauth_core_tests)

    add_executable(cauth_endpoint_route_cache_tests tests/endpoint_route_cache_tests.cpp)
    target_link_libraries(cauth_endpoint_route_cache_tests PRIVATE cauth::core)
    target_include_directories(cauth_endpoint_route_cache_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_endpoint_route_cache_tests COMMAND cauth_endpoint_route_cache_tests)

    add_executable(cauth_transfer_checkpoint_tests tests/transfer_checkpoint_tests.cpp)
    target_link_libraries(cauth_transfer_checkpoint_tests PRIVATE cauth::core)
    target_include_directories(cauth_transfer_checkpoint_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_transfer_checkpoint_tests COMMAND cauth_transfer_checkpoint_tests)

    add_executable(cauth_public_module_headers_tests tests/public_module_headers_tests.cpp)
    target_link_libraries(cauth_public_module_headers_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_public_module_headers_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_public_module_headers_tests COMMAND cauth_public_module_headers_tests)

    add_executable(cauth_steam_cloud_tests tests/steam_cloud_tests.cpp)
    target_link_libraries(cauth_steam_cloud_tests PRIVATE cauth::steam_cloud)
    target_include_directories(cauth_steam_cloud_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_steam_cloud_tests COMMAND cauth_steam_cloud_tests)

    add_executable(cauth_core_websocket_tests tests/core_websocket_client_tests.cpp)
    target_link_libraries(cauth_core_websocket_tests PRIVATE cauth::core)
    target_include_directories(cauth_core_websocket_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_core_websocket_tests COMMAND cauth_core_websocket_tests)

    add_executable(cauth_credentials_tests tests/credential_store_tests.cpp)
    target_link_libraries(cauth_credentials_tests PRIVATE cauth::core)
    target_include_directories(cauth_credentials_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_credentials_tests COMMAND cauth_credentials_tests)

    add_executable(cauth_login_tests tests/steam_login_service_tests.cpp)
    target_link_libraries(cauth_login_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_login_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_login_tests COMMAND cauth_login_tests)

    add_executable(cauth_steam_auth_provider_tests tests/steam_auth_provider_tests.cpp)
    target_link_libraries(cauth_steam_auth_provider_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_steam_auth_provider_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_steam_auth_provider_tests COMMAND cauth_steam_auth_provider_tests)

    add_executable(cauth_steam_network_authenticator_tests
        tests/steam_network_authenticator_tests.cpp
    )
    target_link_libraries(cauth_steam_network_authenticator_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_steam_network_authenticator_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(
        NAME cauth_steam_network_authenticator_tests
        COMMAND cauth_steam_network_authenticator_tests
    )

    add_executable(cauth_steam_mobile_app_login_tests
        tests/steam_mobile_app_login_tests.cpp
    )
    target_link_libraries(cauth_steam_mobile_app_login_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_steam_mobile_app_login_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(
        NAME cauth_steam_mobile_app_login_tests
        COMMAND cauth_steam_mobile_app_login_tests
    )

    add_executable(cauth_steam_web_cookie_service_tests
        tests/steam_web_cookie_service_tests.cpp
    )
    target_link_libraries(cauth_steam_web_cookie_service_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_steam_web_cookie_service_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(
        NAME cauth_steam_web_cookie_service_tests
        COMMAND cauth_steam_web_cookie_service_tests
    )

    add_executable(cauth_steam_web_api_transport_tests
        tests/steam_web_api_transport_tests.cpp
    )
    target_link_libraries(cauth_steam_web_api_transport_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_steam_web_api_transport_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(
        NAME cauth_steam_web_api_transport_tests
        COMMAND cauth_steam_web_api_transport_tests
    )

    add_executable(cauth_cm_authentication_transport_tests
        tests/cm_authentication_transport_tests.cpp
    )
    target_link_libraries(cauth_cm_authentication_transport_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_authentication_transport_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(
        NAME cauth_cm_authentication_transport_tests
        COMMAND cauth_cm_authentication_transport_tests
    )

    add_executable(cauth_cm_client_hello_tests tests/cm_client_hello_tests.cpp)
    target_link_libraries(cauth_cm_client_hello_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_client_hello_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_cm_client_hello_tests COMMAND cauth_cm_client_hello_tests)

    add_executable(cauth_cm_directory_tests tests/cm_directory_tests.cpp)
    target_link_libraries(cauth_cm_directory_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_directory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_directory_tests COMMAND cauth_cm_directory_tests)

    add_executable(cauth_cm_websocket_tests tests/cm_websocket_tests.cpp)
    target_link_libraries(cauth_cm_websocket_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_websocket_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_websocket_tests COMMAND cauth_cm_websocket_tests)

    add_executable(cauth_cm_message_tests tests/cm_message_tests.cpp)
    target_link_libraries(cauth_cm_message_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_message_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_message_tests COMMAND cauth_cm_message_tests)

    add_executable(cauth_cm_service_method_tests tests/cm_service_method_tests.cpp)
    target_link_libraries(cauth_cm_service_method_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_service_method_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_cm_service_method_tests COMMAND cauth_cm_service_method_tests)

    add_executable(cauth_cm_session_tests tests/cm_session_tests.cpp)
    target_link_libraries(cauth_cm_session_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_session_tests COMMAND cauth_cm_session_tests)

    add_executable(cauth_cm_logon_tests tests/cm_logon_tests.cpp)
    target_link_libraries(cauth_cm_logon_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_logon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_logon_tests COMMAND cauth_cm_logon_tests)

    add_executable(cauth_cm_heartbeat_tests tests/cm_heartbeat_tests.cpp)
    target_link_libraries(cauth_cm_heartbeat_tests PRIVATE cauth::steam_auth)
    target_include_directories(cauth_cm_heartbeat_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cm_heartbeat_tests COMMAND cauth_cm_heartbeat_tests)

    add_executable(cauth_pics_tests tests/pics_tests.cpp)
    target_link_libraries(cauth_pics_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_pics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_pics_tests COMMAND cauth_pics_tests)

    add_executable(cauth_depot_key_tests tests/depot_key_tests.cpp)
    target_link_libraries(cauth_depot_key_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_depot_key_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_depot_key_tests COMMAND cauth_depot_key_tests)

    add_executable(cauth_depot_preflight_tests tests/depot_preflight_tests.cpp)
    target_link_libraries(cauth_depot_preflight_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_depot_preflight_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_depot_preflight_tests COMMAND cauth_depot_preflight_tests)

    add_executable(cauth_manifest_request_code_tests tests/manifest_request_code_tests.cpp)
    target_link_libraries(cauth_manifest_request_code_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_manifest_request_code_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME cauth_manifest_request_code_tests COMMAND cauth_manifest_request_code_tests)

    add_executable(cauth_cdn_manifest_tests tests/cdn_manifest_tests.cpp)
    target_link_libraries(cauth_cdn_manifest_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_cdn_manifest_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cdn_manifest_tests COMMAND cauth_cdn_manifest_tests)

    add_executable(cauth_depot_manifest_tests tests/depot_manifest_tests.cpp)
    target_link_libraries(cauth_depot_manifest_tests PRIVATE cauth::steam_depot)
    target_include_directories(cauth_depot_manifest_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_depot_manifest_tests COMMAND cauth_depot_manifest_tests)

    add_executable(cauth_cli_tests
        tests/cli_tests.cpp
        src/cli/cli_support.cpp
        src/cli/steam_cli.cpp
        src/cli/steam_auth_cli.cpp
        src/cli/steam_depot_cli.cpp
        src/cli/steam_cloud_cli.cpp
    )
    target_link_libraries(cauth_cli_tests PRIVATE cauth::core cauth::steam_auth cauth::steam_depot cauth::steam_cloud)
    target_include_directories(cauth_cli_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME cauth_cli_tests COMMAND cauth_cli_tests)

    if(CAUTH_BUILD_FFI)
        add_executable(cauth_steam_depot_ffi_tests tests/steam_depot_ffi_tests.cpp)
        target_link_libraries(cauth_steam_depot_ffi_tests PRIVATE cauth::core_ffi cauth::steam_depot_ffi)
    add_test(NAME cauth_steam_depot_ffi_tests COMMAND cauth_steam_depot_ffi_tests)

        add_executable(cauth_steam_cloud_ffi_tests tests/steam_cloud_ffi_tests.cpp)
        target_link_libraries(cauth_steam_cloud_ffi_tests PRIVATE cauth::core_ffi cauth::steam_cloud_ffi)
        add_test(NAME cauth_steam_cloud_ffi_tests COMMAND cauth_steam_cloud_ffi_tests)
    endif()
endif()

install(
    TARGETS cauth_core
    EXPORT CAuthCoreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS cauth_steam_auth
    EXPORT CAuthSteamAuthTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS cauth_steam_depot cauth_steam
    EXPORT CAuthSteamDepotTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TARGET cauth_zstd_static)
    install(
        TARGETS cauth_zstd_static
        EXPORT CAuthSteamDepotTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(TARGET cauth_zlib_static)
    install(
        TARGETS cauth_zlib_static
        EXPORT CAuthSteamDepotTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

install(
    TARGETS cauth_steam_cloud
    EXPORT CAuthSteamCloudTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(CAUTH_BUILD_FFI)
    install(
        TARGETS cauth_core_ffi
        EXPORT CAuthCoreTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_CORE}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        TARGETS cauth_steam_auth_ffi
        EXPORT CAuthSteamAuthTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        TARGETS cauth_steam_depot_ffi
        EXPORT CAuthSteamDepotTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        TARGETS cauth_steam_cloud_ffi
        EXPORT CAuthSteamCloudTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(CAUTH_BUILD_CLI)
    install(
        TARGETS cauth
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT ${CAUTH_COMPONENT_CLI}
    )
endif()

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/core.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/core_ffi.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/ffi_base.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cauth
    COMPONENT ${CAUTH_COMPONENT_CORE}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/core/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/core
    COMPONENT ${CAUTH_COMPONENT_CORE}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_auth.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_auth_ffi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cauth
    COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/steam/auth/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/steam/auth
    COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/steam/cm/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/steam/cm
    COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_depot.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_depot_ffi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cauth
    COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_cloud.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cauth/steam_cloud_ffi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cauth
    COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/steam/depot/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/steam/depot
    COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/steam/cloud/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/steam/cloud
    COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    EXPORT CAuthCoreTargets
    FILE CAuthCoreTargets.cmake
    NAMESPACE cauth::
    DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
    COMPONENT ${CAUTH_COMPONENT_CORE}
)

install(
    EXPORT CAuthSteamAuthTargets
    FILE CAuthSteamAuthTargets.cmake
    NAMESPACE cauth::
    DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
    COMPONENT ${CAUTH_COMPONENT_STEAM_AUTH}
)

install(
    EXPORT CAuthSteamDepotTargets
    FILE CAuthSteamDepotTargets.cmake
    NAMESPACE cauth::
    DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
    COMPONENT ${CAUTH_COMPONENT_STEAM_DEPOT}
)

install(
    EXPORT CAuthSteamCloudTargets
    FILE CAuthSteamCloudTargets.cmake
    NAMESPACE cauth::
    DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
    COMPONENT ${CAUTH_COMPONENT_STEAM_CLOUD}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CAuthConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CAuthConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CAuthConfig.cmake"
    INSTALL_DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CAuthConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CAuthConfigVersion.cmake"
    DESTINATION ${CAUTH_INSTALL_CMAKEDIR}
    COMPONENT ${CAUTH_COMPONENT_CORE}
)
