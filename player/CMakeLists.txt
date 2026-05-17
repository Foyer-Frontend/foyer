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

# foyer's libretro frontend lives in shared/ but only wires up here when the
# player target needs it. We compile it as a small static lib alongside the
# core so identical code is reused across player builds.
#
# Phase ImGui-1 skips this entire block — the "Hello foyer" boot doesn't
# touch libretro yet, and the existing sources still call nanovg
# (`nvgUpdateImage` etc.) which the ImGui shell won't provide until
# Phase 2 brings in shared/libretro/video_gl.cpp.
if (NOT PLAYER_IMGUI)
add_library(${_frontend} STATIC
    ${CMAKE_SOURCE_DIR}/shared/libretro/frontend.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/video_hw.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/audio.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/input.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/savestate.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/overlay.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheevos.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/core_options.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/cheats.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/bezel.cpp
    ${CMAKE_SOURCE_DIR}/shared/libretro/shader.cpp
)
target_include_directories(${_frontend} PUBLIC
    ${CMAKE_SOURCE_DIR}/shared/libretro
    ${CMAKE_SOURCE_DIR}/shared
)
# Cores that ship their own embedded rcheevos inside their libretro
# static archive (PPSSPP, future flycast revisions, etc.) opt into the
# headers-only path by setting FOYER_CORE_EMBEDS_RCHEEVOS in their recipe.
# Everyone else links foyer's shared rcheevos lib.
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

# Pick the render shell for this player. 3-way:
#   PLAYER_IMGUI  -> Dear ImGui + libnx-EGL (Phase ImGui-1+, see
#                   docs/PLAYER_MIGRATION_PLAN.md)
#   PLAYER_BRLS   -> borealis (Phase H, current default)
#   neither       -> legacy foyer_render (nanovg-deko3d App)
# brls and ImGui cannot coexist in one binary — they pull different
# nanovg copies and fight the nvhost channel — so the top-level
# CMakeLists.txt forces PLAYER_BRLS=OFF whenever PLAYER_IMGUI=ON.
if (PLAYER_IMGUI)
    # foyer_imgui already pulls EGL/GLESv2 + the OpenGL3 backend; the
    # libretro frontend lib still needs GL symbols on Switch (HW
    # render path through video_hw.cpp), so we leave the EGL/GLESv2/
    # glapi/drm_nouveau libs linked below.
    set(_render_lib foyer_imgui)
elseif (PLAYER_BRLS)
    set(_render_lib borealis)
else()
    set(_render_lib foyer_render)
endif()

target_link_libraries(${_frontend} PUBLIC
    foyer_shared
    ${_render_lib}
    core_${FOYER_CORE}
    ${_rcheevos_dep}
    # EGL + GLES3 + Mesa stack for the HW render callback path. Even
    # pure-software cores link these (the HwContext singleton is part
    # of the frontend lib); the libs sit cold until a core actually
    # requests SET_HW_RENDER. Order matters: EGL calls glapi which
    # calls into the nouveau driver which uses drm_nouveau.
    EGL
    GLESv2
    glapi
    drm_nouveau
)
set_target_properties(${_frontend} PROPERTIES
    CXX_STANDARD 26)
endif()  # NOT PLAYER_IMGUI

# Player executable. The render shell choice (above) swaps the entry
# + activity sources. Each shell ships its own userAppInit/userAppExit:
# borealis brings switch_wrapper.c; ImGui provides them inline in
# main_imgui.cpp; foyer_render uses foyer_shared's services.cpp.
if (PLAYER_IMGUI)
    add_executable(${_player}
        src/imgui/main_imgui.cpp
        src/imgui/gl_context.cpp
        src/imgui/imgui_switch_input.cpp
        src/imgui/imgui_theme.cpp
    )
    target_include_directories(${_player} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
elseif (PLAYER_BRLS)
    add_executable(${_player}
        src/main_brls.cpp
        src/emulator_activity.cpp
        src/emulator_view.cpp
        src/pause_activity.cpp
        src/slot_picker_activity.cpp
        src/display_picker_activity.cpp
        src/core_options_picker_activity.cpp
        src/shaders_picker_activity.cpp
        src/cheats_picker_activity.cpp

        # borealis Switch platform glue. Provides userAppInit
        # (including romfsInit) and the deko3d-side service
        # initialisation brls needs before its first frame.
        # Linker prefers this direct .o over foyer_shared.a's
        # services.cpp (which the player binary doesn't pull
        # otherwise), so there's no multi-def conflict.
        ${borealis_SOURCE_DIR}/library/lib/platforms/switch/switch_wrapper.c
    )
    target_include_directories(${_player} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
else()
    add_executable(${_player}
        src/main.cpp
    )
endif()
if (PLAYER_IMGUI)
    # Phase ImGui-1 boots a black-screen "Hello foyer" without
    # libretro. The frontend lib still uses nanovg APIs throughout
    # (video.cpp::nvgUpdateImage etc.) which require either brls or
    # foyer_render to provide nanovg; in ImGui-2 we replace those
    # call sites with raw GLES via shared/libretro/video_gl.cpp and
    # re-link the frontend.
    target_link_libraries(${_player} PRIVATE
        foyer_shared
        foyer_imgui
        EGL
        GLESv2
        glapi
        drm_nouveau
    )
else()
    target_link_libraries(${_player} PRIVATE
        ${_frontend}
    )
endif()
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
# PLAYER_BRLS depends on brls's bundled i18n / fonts / material
# / XML cells under romfs:/. Without those, Application::init's
# i18n loader fails to find romfs:/i18n/<locale>/brls.json, the
# nlohmann::json parse throws, and (with -fno-exceptions) the
# player diagAbortWithResults at startup — that was the
# "all cores crash on launch" report. Same pattern the browser
# uses (browser/CMakeLists.txt), kept in sync.
if (PLAYER_BRLS)
    file(COPY ${borealis_SOURCE_DIR}/resources/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/romfs)
endif()
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
