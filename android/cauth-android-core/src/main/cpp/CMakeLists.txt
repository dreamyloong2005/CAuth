cmake_minimum_required(VERSION 3.22)

project(cauth_android_jni LANGUAGES C CXX)

set(CAUTH_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(CAUTH_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CAUTH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAUTH_BUILD_FFI ON CACHE BOOL "" FORCE)

add_subdirectory("${CAUTH_ROOT_DIR}" "${CMAKE_BINARY_DIR}/cauth-native")

add_library(cauth_android_core_jni SHARED
    cauth_android_core_jni.cpp
)

target_include_directories(cauth_android_core_jni
    PRIVATE
        "${CAUTH_ROOT_DIR}/include"
        "${CAUTH_ROOT_DIR}/src"
)

target_link_libraries(cauth_android_core_jni
    PRIVATE
        cauth::core_ffi
        cauth::core
        android
        log
)
