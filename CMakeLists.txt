cmake_minimum_required(VERSION 3.20)
project(linyaps_store VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_library(linyaps_store STATIC lib/linyaps_backend.c)
target_include_directories(linyaps_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(linyaps_store PUBLIC PkgConfig::SYSTEMD)

add_executable(test_backend tests/test_backend.c)
target_link_libraries(test_backend PRIVATE linyaps_store)

add_executable(linyaps_cli tests/cli_test.c)
target_link_libraries(linyaps_cli PRIVATE linyaps_store)

enable_testing()
add_test(NAME test_backend COMMAND test_backend)
