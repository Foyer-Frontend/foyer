# foyer-<core>.nro — per-system player binary.
#
# This subdir can be add_subdirectory()'d once per core in a single configure.
# Top-level CMakeLists.txt is responsible for setting FOYER_CORE before each
# include and giving us a unique binary_dir (so artifacts don't collide). All
# targets we declare are suffixed with _${FOYER_CORE} for the same reason.
#
# rcheevos is built once at top-level (it's the same library across cores) and
# linked in here.

if (NOT FOYER_CORE)
    message(FATAL_ERROR "player/ included without FOYER_CORE set")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/core_recipe.cmake)

# Pull in the per-core recipe; it must define a static lib `core_${FOYER_CORE}`.
set(_core_file ${CMAKE_SOURCE_DIR}/cores/${FOYER_CORE}.cmake)
if (NOT EXISTS ${_core_file})
    message(FATAL_ERROR "No core recipe at ${_core_file}")
endif()
include(${_core_file})

set(_frontend foyer_libretro_frontend_${FOYER_CORE})
set(_player   foyer_player_${FOYER_CORE})
set(_romfs    foyer_player_romfs_${FOYER_CORE})

# foyer's libretro frontend lives in shared/ but only wires up here when the
# player target needs it. We compile it as a small static lib alongside the
# core so identical code is reused across player builds.
add_library(${_frontend} STATIC
    ${CMAKE_SOURCE_DIR}/shared/libretro/frontend.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/audio.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/input.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/savestate.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/overlay.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheevos.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/core_options.cpp
)
target_include_directories(${_frontend} PUBLIC
    ${CMAKE_SOURCE_DIR}/shared/libretro
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(${_frontend} PUBLIC
    foyer_shared
    core_${FOYER_CORE}
    rcheevos
)
set_target_properties(${_frontend} PROPERTIES
    CXX_STANDARD 26)

# Player executable.
add_executable(${_player}
    src/main.cpp
)
target_link_libraries(${_player} PRIVATE
    ${_frontend}
)
target_compile_definitions(${_player} PRIVATE
    FOYER_CORE_NAME=${FOYER_CORE})
set_target_properties(${_player} PROPERTIES
    OUTPUT_NAME foyer-${FOYER_CORE}
    CXX_STANDARD 26)

target_compile_options(${_player} PRIVATE
    --embed-dir=${FOYER_EMBED_DIR})

file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
dkp_add_asset_target(${_romfs} ${CMAKE_CURRENT_BINARY_DIR}/romfs)
dkp_install_assets(${_romfs}
    DESTINATION shaders
    TARGETS fill_aa_fsh fill_fsh fill_vsh)

nx_generate_nacp(
    OUTPUT  ${_player}.nacp
    NAME    foyer-${FOYER_CORE}
    AUTHOR  foyer
    VERSION ${PROJECT_VERSION})

nx_create_nro(${_player}
    OUTPUT ${CMAKE_BINARY_DIR}/foyer-${FOYER_CORE}.nro
    ICON   ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP   ${_player}.nacp
    ROMFS  ${_romfs})

message(STATUS "player nro -> ${CMAKE_BINARY_DIR}/foyer-${FOYER_CORE}.nro")
