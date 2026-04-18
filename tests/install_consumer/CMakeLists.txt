cmake_minimum_required(VERSION 3.22)

project(cauth_install_consumer LANGUAGES C CXX)

find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth steam_depot)

add_executable(cauth_install_consumer main.cpp)
target_link_libraries(cauth_install_consumer
    PRIVATE
        cauth::core
        cauth::steam_auth
        cauth::steam_depot
        cauth::core_ffi
        cauth::steam_auth_ffi
        cauth::steam_depot_ffi
)
