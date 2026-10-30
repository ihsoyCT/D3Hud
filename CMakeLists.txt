cmake_minimum_required(VERSION 3.20)
project(D3Hud VERSION 0.1.0 LANGUAGES CXX)

# The release folder must run on a machine without the Visual C++ redistributable: link the
# MSVC runtime statically (must be set before any target is created, imgui included).
option(D3HUD_STATIC_RUNTIME "Link the MSVC runtime statically (self-contained exes)" ON)
if(MSVC AND D3HUD_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/imgui)

add_library(d3hud_core STATIC
    src/core/SceneGen.cpp
    src/core/ReplayIO.cpp
    src/core/SharedRing.cpp
    src/core/SnapshotInterp.cpp
    src/core/SceneCells.cpp)
target_include_directories(d3hud_core PUBLIC src/core)
target_compile_definitions(d3hud_core PUBLIC _CRT_SECURE_NO_WARNINGS)

add_executable(D3Hud.Overlay
    src/overlay/main.cpp
    src/overlay/HudRenderer.cpp src/overlay/ReaderProcess.cpp src/overlay/SalvageRunner.cpp)
target_include_directories(D3Hud.Overlay PRIVATE src/overlay src/core)
target_link_libraries(D3Hud.Overlay PRIVATE d3hud_core imgui d3d11 dxgi dxguid dcomp)

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

add_executable(d3hud_tests src/tests/test_core.cpp)
target_link_libraries(d3hud_tests PRIVATE d3hud_core d3hud_reader)

enable_testing()
add_test(NAME core COMMAND d3hud_tests)

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

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME D3Hud)
set(CPACK_PACKAGE_FILE_NAME "D3Hud-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
