cmake_minimum_required(VERSION 3.22)

project(cauth_install_consumer_core LANGUAGES C CXX)

find_package(CAuth CONFIG REQUIRED COMPONENTS core)

add_executable(cauth_install_consumer_core main.cpp)
target_link_libraries(cauth_install_consumer_core PRIVATE cauth::core cauth::core_ffi)
