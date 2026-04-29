# foyer-<core>.nro — per-system player binary.
#
# Selected at configure time via FOYER_CORE=<name>. Each invocation produces a
# single nro that bundles foyer's libretro frontend + that one core's static
# library. The browser nro launches us via envSetNextLoad() with the rom path
# as argv[1]; on exit hbloader chains back to the browser (which is installed
# as /hbmenu.nro).

include(${CMAKE_SOURCE_DIR}/cmake/core_recipe.cmake)

# Pull in the per-core recipe; it must define a static lib `core_${FOYER_CORE}`.
set(_core_file ${CMAKE_SOURCE_DIR}/cores/${FOYER_CORE}.cmake)
if (NOT EXISTS ${_core_file})
    message(FATAL_ERROR "No core recipe at ${_core_file}")
endif()
include(${_core_file})

# RetroAchievements client (shared by every player nro).
include(${CMAKE_SOURCE_DIR}/cores/rcheevos.cmake)

# foyer's libretro frontend lives in shared/ but only wires up here when the
# player target needs it. We compile it as a small static lib alongside the
# core so identical code is reused across player builds.
add_library(foyer_libretro_frontend STATIC
    ${CMAKE_SOURCE_DIR}/shared/libretro/frontend.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/audio.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/input.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/savestate.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/overlay.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheevos.cpp
)
target_include_directories(foyer_libretro_frontend PUBLIC
    ${CMAKE_SOURCE_DIR}/shared/libretro
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(foyer_libretro_frontend PUBLIC
    foyer_shared
    core_${FOYER_CORE}
    rcheevos
)
set_target_properties(foyer_libretro_frontend PROPERTIES
    CXX_STANDARD 26)

# Player executable.
add_executable(foyer_player
    src/main.cpp
)
target_link_libraries(foyer_player PRIVATE
    foyer_libretro_frontend
)
target_compile_definitions(foyer_player PRIVATE
    FOYER_CORE_NAME=${FOYER_CORE})
set_target_properties(foyer_player PROPERTIES
    OUTPUT_NAME foyer-${FOYER_CORE}
    CXX_STANDARD 26)

target_compile_options(foyer_player PRIVATE
    --embed-dir=${FOYER_EMBED_DIR})

file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
dkp_add_asset_target(foyer_player_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)
dkp_install_assets(foyer_player_romfs
    DESTINATION shaders
    TARGETS fill_aa_fsh fill_fsh fill_vsh)

nx_generate_nacp(
    OUTPUT  foyer_player.nacp
    NAME    foyer-${FOYER_CORE}
    AUTHOR  foyer
    VERSION ${PROJECT_VERSION})

nx_create_nro(foyer_player
    OUTPUT ${CMAKE_BINARY_DIR}/foyer-${FOYER_CORE}.nro
    ICON   ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP   foyer_player.nacp
    ROMFS  foyer_player_romfs)

message(STATUS "player nro -> ${CMAKE_BINARY_DIR}/foyer-${FOYER_CORE}.nro")
