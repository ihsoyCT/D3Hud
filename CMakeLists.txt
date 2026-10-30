cmake_minimum_required(VERSION 3.20)
project(D3Hud VERSION 0.1.0 LANGUAGES C CXX)

# The release folder must run on a machine without the Visual C++ redistributable: link the
# MSVC runtime statically (must be set before any target is created, imgui included).
option(D3HUD_STATIC_RUNTIME "Link the MSVC runtime statically (self-contained exes)" ON)
if(MSVC AND D3HUD_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(third_party/imgui)

# Lua 5.4 (MIT), vendored; only the pure-language parts: no io/os/package/debug libraries.
# Compiled as C++ on purpose: Lua then raises errors as C++ exceptions, so the destructors of our bindings run
# (as C it uses longjmp, which skips them and crashes through frames that own std::string and the like).
file(GLOB LUA_SOURCES third_party/lua/src/*.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC third_party/lua/src)
target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)

add_library(d3hud_core STATIC
    src/core/SceneGen.cpp
    src/core/ReplayIO.cpp
    src/core/SharedRing.cpp
    src/core/SnapshotInterp.cpp
    src/core/SceneCells.cpp)
target_include_directories(d3hud_core PUBLIC src/core)
target_compile_definitions(d3hud_core PUBLIC _CRT_SECURE_NO_WARNINGS)

# Plugin runtime: Lua sandbox + the host API binding (no UI, so it is unit-tested).
add_library(d3hud_plugins STATIC src/plugins/LuaPlugin.cpp)
target_include_directories(d3hud_plugins PUBLIC src/plugins)
target_link_libraries(d3hud_plugins PUBLIC d3hud_core lua)

add_executable(D3Hud.Overlay
    src/overlay/main.cpp
    src/overlay/HudRenderer.cpp src/overlay/ReaderProcess.cpp src/overlay/SalvageRunner.cpp
    src/overlay/PluginHost.cpp)
target_include_directories(D3Hud.Overlay PRIVATE src/overlay src/core)
target_link_libraries(D3Hud.Overlay PRIVATE d3hud_core d3hud_plugins imgui d3d11 dxgi dxguid dcomp)
# Lua plugins are loaded from <exe>/plugins/<name>/
add_custom_command(TARGET D3Hud.Overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:D3Hud.Overlay>/plugins)

add_library(d3hud_reader STATIC
    src/reader/Process.cpp
    src/reader/AobScanner.cpp
    src/reader/Offsets.cpp
    src/reader/PlayerTracker.cpp
    src/reader/ActorList.cpp
    src/reader/AcdTable.cpp
    src/reader/AttribTable.cpp
    src/reader/UiControls.cpp
    src/reader/SceneTable.cpp
    src/reader/SnoTable.cpp
    src/reader/SharedScan.cpp
    src/reader/BasesCache.cpp)
target_include_directories(d3hud_reader PUBLIC src/reader)
target_link_libraries(d3hud_reader PUBLIC d3hud_core)

add_executable(D3Hud.Reader src/reader/main.cpp)
target_link_libraries(D3Hud.Reader PRIVATE d3hud_reader winmm)  # winmm: 1 ms timer for 60 Hz
# data tables loaded at runtime from next to the exes (reader and overlay share a dir):
# actor SNOs, item names, scene shapes (map prediction)
add_custom_command(TARGET D3Hud.Reader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/analysis/sno_actors.tsv $<TARGET_FILE_DIR:D3Hud.Reader>/sno_actors.tsv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/analysis/items_gbid.tsv $<TARGET_FILE_DIR:D3Hud.Reader>/items_gbid.tsv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/analysis/scene_cells.bin $<TARGET_FILE_DIR:D3Hud.Reader>/scene_cells.bin)

add_executable(d3hud_tests src/tests/test_core.cpp src/tests/host_api_c.c)
target_link_libraries(d3hud_tests PRIVATE d3hud_core d3hud_reader d3hud_plugins)
target_compile_definitions(d3hud_tests PRIVATE D3HUD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

enable_testing()
add_test(NAME core COMMAND d3hud_tests)

# Warnings for our own code (not the vendored imgui). /permissive- = standard conformance.
if(MSVC)
    foreach(t d3hud_core d3hud_plugins d3hud_reader D3Hud.Overlay D3Hud.Reader d3hud_tests)
        target_compile_options(${t} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endforeach()
endif()

# --- install: one relocatable folder -------------------------------------------------------
# cmake --install build --config Release            -> <source>/dist
# cmake --install build --config Release --prefix X -> X
# The exes find their data files, d3hud.ini and reader.log next to themselves, so the folder
# can be moved anywhere (keep it somewhere writable). `cpack -C Release` (run in build/) makes
# a zip of the same layout.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Install prefix" FORCE)
endif()
install(TARGETS D3Hud.Overlay D3Hud.Reader RUNTIME DESTINATION .)
install(FILES
    analysis/sno_actors.tsv
    analysis/items_gbid.tsv
    analysis/scene_cells.bin
    README.md
    DESTINATION .)
install(FILES third_party/imgui/LICENSE.txt DESTINATION licenses RENAME imgui-LICENSE.txt)
install(FILES third_party/lua/LICENSE.txt DESTINATION licenses RENAME lua-LICENSE.txt)
install(DIRECTORY plugins DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME D3Hud)
set(CPACK_PACKAGE_FILE_NAME "D3Hud-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
