cmake_minimum_required(VERSION 3.20)
project(linyaps_store VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable POSIX / GNU extensions for gettimeofday, localtime_r, etc.
add_compile_definitions(_GNU_SOURCE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(CURL    REQUIRED IMPORTED_TARGET libcurl)

add_library(cjson STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cjson/cJSON.c)
target_include_directories(cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cjson)

add_library(linyaps_store STATIC
    lib/linyaps_util.c
    lib/linyaps_dbus_msg.c
    lib/linyaps_task.c
    lib/linyaps_context.c
    lib/linyaps_cli.c
    lib/linyaps_remote.c)
target_include_directories(linyaps_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(linyaps_store PUBLIC PkgConfig::SYSTEMD PkgConfig::CURL PRIVATE cjson)

add_executable(test_backend tests/test_backend.c)
target_link_libraries(test_backend PRIVATE linyaps_store)

add_executable(linyaps_cli tests/cli_test.c)
target_link_libraries(linyaps_cli PRIVATE linyaps_store)

enable_testing()
add_test(NAME test_backend COMMAND test_backend)

# ---- KilnUI (self-contained subdir, demos disabled) ----
option(KILNUI_BUILD_DEMOS "Build KilnUI demos" OFF)
add_subdirectory(kilnui)

# ---- Store GUI application ----
add_executable(linyaps_store_app
    ui/main.c
    ui/store_state.c
    ui/store_ui.c
    ui/views/sidebar.c
    ui/views/titlebar.c
    ui/views/app_grid.c
    ui/views/updates_view.c
    ui/views/ranking_view.c)

target_include_directories(linyaps_store_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/kilnui/src
    ${CMAKE_CURRENT_SOURCE_DIR}/kilnui/3rdparty/clay)

target_link_libraries(linyaps_store_app PRIVATE
    linyaps_store
    kilnui::kilnui
    kilnui::ui_components
    pthread)

# Copy shader directory next to the binary so the renderer finds SPIRVs
add_dependencies(linyaps_store_app shaders)
add_custom_command(TARGET linyaps_store_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:linyaps_store_app>/shaders
    COMMENT "Copying shaders to output directory")
