# foyer-<core>.nro — per-system player binary.
#
# Plutonium (SDL2 + XorTroll/Plutonium widgets) since 0.7.0. The
# legacy brls / ImGui / nanovg shells were removed in P5 — see
# docs/PLAYER_MIGRATION_PLAN.md for the history.
#
# This subdir can be add_subdirectory()'d once per core in a single
# configure. Top-level CMakeLists.txt is responsible for setting
# FOYER_CORE before each include and giving us a unique binary_dir
# (so artifacts don't collide). All targets we declare are suffixed
# with _${FOYER_CORE} for the same reason.
#
# rcheevos is built once at top-level (it's the same library across
# cores) and linked in here.

if (NOT FOYER_CORE)
    message(FATAL_ERROR "player/ included without FOYER_CORE set")
endif()

include(${FOYER_CORES_DIR}/cmake/core_recipe.cmake)

# Pull in the per-core recipe; it must define a static lib `core_${FOYER_CORE}`.
set(_core_file ${FOYER_CORES_DIR}/recipes/${FOYER_CORE}.cmake)
if (NOT EXISTS ${_core_file})
    message(FATAL_ERROR "No core recipe at ${_core_file}")
endif()
include(${_core_file})

set(_frontend foyer_libretro_frontend_${FOYER_CORE})
set(_player   foyer_player_${FOYER_CORE})
set(_romfs    foyer_player_romfs_${FOYER_CORE})

# foyer's libretro frontend lives in shared/ but only wires up here
# when the player target needs it. Compiled as a small static lib
# alongside the core so identical code is reused across player
# builds.
#
# SDL sinks: video_sdl + bezel_sdl + audio_sdl. input.cpp stays on
# libnx pad directly — SDL_GameController on switch-sdl2 mis-mapped
# the L3 stick-click as the A button, so we bypass it and let SDL2
# only own the controller for Plutonium's nav.
add_library(${_frontend} STATIC
    ${CMAKE_SOURCE_DIR}/shared/libretro/frontend.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video_hw.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/savestate.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheevos.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/core_options.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheats.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/shader.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video_sdl.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/bezel_sdl.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/audio_sdl.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/input.cpp
)
target_include_directories(${_frontend} PUBLIC
    ${CMAKE_SOURCE_DIR}/shared/libretro
    ${CMAKE_SOURCE_DIR}/shared
)
# Cores that ship their own embedded rcheevos inside their libretro
# static archive (PPSSPP, future flycast revisions, etc.) opt into
# the headers-only path by setting FOYER_CORE_EMBEDS_RCHEEVOS in
# their recipe. Everyone else links foyer's shared rcheevos lib.
if (FOYER_CORE_EMBEDS_RCHEEVOS)
    set(_rcheevos_dep rcheevos_headers)
else()
    set(_rcheevos_dep rcheevos)
endif()

target_compile_definitions(${_frontend} PRIVATE
    # frontend.cpp's CoreOptions::set_core_name() lookup is gated on
    # this macro. Without it, m_core_name stays empty and the per-
    # core overrides JSONC resolves to /foyer/config/cores/unknown.jsonc
    # — every core's options collapse into one shared file and the
    # in-app per-core defaults can't apply.
    FOYER_CORE_NAME=${FOYER_CORE})

target_link_libraries(${_frontend} PUBLIC
    foyer_shared
    foyer_plutonium
    core_${FOYER_CORE}
    ${_rcheevos_dep}
    # EGL + GLES3 + Mesa stack for the HW render callback path. Even
    # pure-software cores link these (the HwContext singleton is
    # part of the frontend lib); the libs sit cold until a core
    # actually requests SET_HW_RENDER. Order matters: EGL calls
    # glapi which calls into the nouveau driver which uses
    # drm_nouveau.
    EGL
    GLESv2
    glapi
    drm_nouveau
)
set_target_properties(${_frontend} PROPERTIES
    CXX_STANDARD 26)

# Player executable. Plutonium provides userAppInit / userAppExit
# via foyer_plutonium and our own main_plutonium.cpp entry.
add_executable(${_player}
    src/plutonium/main_plutonium.cpp
    src/plutonium/main_application.cpp
    src/plutonium/emulator_element.cpp
    src/plutonium/pause_menu.cpp
    src/plutonium/session_tracker.cpp
)
target_include_directories(${_player} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${_player} PRIVATE
    ${_frontend}
    foyer_plutonium
)
target_compile_definitions(${_player} PRIVATE
    FOYER_CORE_NAME=${FOYER_CORE})
set_target_properties(${_player} PROPERTIES
    OUTPUT_NAME foyer-${FOYER_CORE}
    CXX_STANDARD 26)

target_compile_options(${_player} PRIVATE
    --embed-dir=${FOYER_EMBED_DIR})

# systems/ + themes/ are browser-only art — they live in the
# downloadable foyer-assets.zip now. Exclude them from player NROs
# so each foyer-<core>.nro stays small.
file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/romfs)
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/romfs
     PATTERN "systems" EXCLUDE
     PATTERN "themes"  EXCLUDE)
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
