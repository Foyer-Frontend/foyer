cmake_minimum_required(VERSION 3.21)

project(foyer
    VERSION 0.7.4
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_BROWSER "Build the foyer browser nro"     ON)
option(BUILD_PLAYER  "Build per-core player nros"      OFF)
# Player binary is Plutonium-only since 0.7.0. The borealis (Phase
# H) and Dear ImGui (Phase ImGui-1..6) player shells were retired
# in P5 — see docs/PLAYER_MIGRATION_PLAN.md for the history.
# FOYER_CORE = single core name (legacy single-build presets).
set(FOYER_CORE  "" CACHE STRING
    "Single libretro core to build (e.g. fceumm). Used by Player-* presets.")
# FOYER_CORES = semicolon-separated core list — produces one foyer-<core>.nro
# target per entry in a single configure. Used by the Players-All preset.
set(FOYER_CORES "" CACHE STRING
    "Semicolon-separated list of cores to build players for.")

if (BUILD_PLAYER AND NOT FOYER_CORE AND NOT FOYER_CORES)
    message(FATAL_ERROR
        "BUILD_PLAYER=ON requires FOYER_CORE=<name> or FOYER_CORES=<a;b;c>")
endif()

# ---------------------------------------------------------------------------
# foyer-cores location
#
# Core build recipes (cmake/<core>.cmake) live in a sibling repo so they can
# rebuild on their own cadence. Defaults to ../foyer-cores; override with
# -DFOYER_CORES_DIR=... or env var.
# ---------------------------------------------------------------------------
if (NOT FOYER_CORES_DIR)
    if (DEFINED ENV{FOYER_CORES_DIR})
        set(FOYER_CORES_DIR "$ENV{FOYER_CORES_DIR}")
    else()
        set(FOYER_CORES_DIR "${CMAKE_SOURCE_DIR}/../foyer-cores")
    endif()
endif()
get_filename_component(FOYER_CORES_DIR "${FOYER_CORES_DIR}" ABSOLUTE)
set(FOYER_CORES_DIR "${FOYER_CORES_DIR}" CACHE PATH
    "Path to the foyer-cores repo containing recipes/ and cmake/core_recipe.cmake.")

if (BUILD_PLAYER)
    if (NOT EXISTS "${FOYER_CORES_DIR}/recipes" OR
        NOT EXISTS "${FOYER_CORES_DIR}/cmake/core_recipe.cmake")
        message(FATAL_ERROR
            "FOYER_CORES_DIR='${FOYER_CORES_DIR}' is missing recipes/ or "
            "cmake/core_recipe.cmake. Clone foyer-cores next to foyer, or pass "
            "-DFOYER_CORES_DIR=<path>.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler config (shared by all targets)
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD            23)
set(CMAKE_C_EXTENSIONS          ON)
set(CMAKE_CXX_STANDARD          26)
set(CMAKE_CXX_EXTENSIONS        ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Embed-dir support (used by --embed-dir for asset blobs)
set(FOYER_EMBED_DIR ${CMAKE_SOURCE_DIR}/assets/embed)

# Capture build-time identity
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE FOYER_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if (NOT FOYER_GIT_COMMIT)
    set(FOYER_GIT_COMMIT "untracked")
endif()
set(FOYER_DISPLAY_VERSION "${PROJECT_VERSION} [${FOYER_GIT_COMMIT}]")

# ---------------------------------------------------------------------------
# External deps fetched up front so all targets can link against them
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG        845c9fc)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        5c20573)

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.12.0)

set(YYJSON_INSTALL                          OFF)
set(YYJSON_DISABLE_UTILS                    ON)
set(YYJSON_DISABLE_FAST_FP_CONV             ON)
set(YYJSON_DISABLE_NON_STANDARD             OFF) # we want JSONC-ish reads
set(YYJSON_DISABLE_UTF8_VALIDATION          ON)
set(YYJSON_DISABLE_UNALIGNED_MEMORY_ACCESS  OFF)

FetchContent_MakeAvailable(nanovg stb yyjson)

add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# System libs from devkitPro
# ---------------------------------------------------------------------------
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

# ---------------------------------------------------------------------------
# borealis (XITRIX fork — used by Moonlight Switch). Browser UI is built
# on top of brls starting in 0.6.0. Player binary uses Plutonium (SDL2)
# instead since 0.7.0 — brls is browser-only now.
# ---------------------------------------------------------------------------
if (BUILD_BROWSER)
    # Switch + deko3d backend. We bypass borealis's toolchain.cmake (it
    # tries to include $DEVKITPRO/cmake/Switch.cmake, which our top-level
    # preset already loaded), so the BOREALIS_USE_* flags must be set
    # explicitly here — the toolchain wrapper would normally do it.
    set(PLATFORM_SWITCH      ON  CACHE BOOL "" FORCE)
    set(USE_DEKO3D           ON  CACHE BOOL "" FORCE)
    set(BOREALIS_USE_DEKO3D  ON  CACHE BOOL "" FORCE)
    set(USE_GLFW             ON  CACHE BOOL "" FORCE)  # input only on Switch
    set(BRLS_UNITY_BUILD     OFF CACHE BOOL "" FORCE)  # avoid empty-prop bug
    # Avoid pulling system fmt/tinyxml2/tweeny — vendor what borealis
    # ships so the devkitPro sysroot doesn't need extra packages.
    set(USE_SYSTEM_FMT       OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_TINYXML2  OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_TWEENY    OFF CACHE BOOL "" FORCE)
    set(BRLS_RESOURCES_DIR   "romfs:/" CACHE STRING "" FORCE)

    # XITRIX/borealis's default branch is moonlight_wiliwili — that's the
    # active fork used by Moonlight Switch. The "main" branch is older and
    # missing CMake support; pin to moonlight_wiliwili explicitly.
    FetchContent_Declare(borealis
        GIT_REPOSITORY https://github.com/XITRIX/borealis.git
        GIT_TAG        moonlight_wiliwili
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  library)
    FetchContent_MakeAvailable(borealis)

    # Carousel-style horizontal wrap: foyer wants left↔right edges to
    # wrap on every horizontal Box. brls upstream hard-stops at edges;
    # we patch box.cpp in place after populate. The script is
    # idempotent + leaves a sentinel so re-configures don't restack.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DBOX_CPP=${borealis_SOURCE_DIR}/library/lib/core/box.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_brls_wraparound.cmake)

    # SwitchAudioPlayer patch DISABLED again. Sharing audren between
    # brls's pulsar (browser UI sounds) and the libretro AudioSink
    # is non-trivial — they collide on voice 0 + the memory pool,
    # producing silence in both. Until we have a proper coordinator
    # (single audrv instance with namespaced voice ids), keep the
    # patch off; cores get audio, browser UI is silent.
    # execute_process(
    #     COMMAND ${CMAKE_COMMAND}
    #         -DAUDIO_CPP=${borealis_SOURCE_DIR}/library/lib/platforms/switch/switch_audio.cpp
    #         -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_brls_audio.cmake)

    # devkitPro's portlibs/switch/include is the canonical location for
    # switch-glm, switch-glfw, EGL, etc. The Switch.cmake toolchain
    # exposes them via find_package, but borealis #includes them
    # directly (e.g. <glm/vec2.hpp> in the deko3d nanovg renderer), so
    # we must put portlibs on the borealis target's system include path.
    if (DEFINED ENV{DEVKITPRO})
        target_include_directories(borealis SYSTEM PRIVATE
            $ENV{DEVKITPRO}/portlibs/switch/include)
    endif()
endif()

# ---------------------------------------------------------------------------
# Plutonium — Switch homebrew UI framework on SDL2 (XorTroll). Used by
# Goldleaf / uLaunch. Player binary's only UI shell since 0.7.0.
# Plutonium ships only a Makefile upstream, so we vendor the source
# via FetchContent and compile it ourselves into a static lib
# alongside our player target. SDL2 + SDL2_image + SDL2_ttf +
# SDL2_mixer + SDL2_gfx come from the devkitpro switch portlibs.
# ---------------------------------------------------------------------------
if (BUILD_PLAYER)
    FetchContent_Declare(plutonium
        GIT_REPOSITORY https://github.com/XorTroll/Plutonium.git
        GIT_TAG        1.0.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(plutonium)

    # Plutonium ships a custom SDL2_ttf "C++ pointer attach" wrapper
    # as a .c source under sdl2/sdl2_CustomTtf.c — needed by every
    # font path. Glob both .cpp + .c.
    file(GLOB_RECURSE _plutonium_sources
        ${plutonium_SOURCE_DIR}/Plutonium/source/*.cpp
        ${plutonium_SOURCE_DIR}/Plutonium/source/*.c)
    add_library(foyer_plutonium STATIC ${_plutonium_sources})
    target_include_directories(foyer_plutonium PUBLIC
        ${plutonium_SOURCE_DIR}/Plutonium/include)
    if (DEFINED ENV{DEVKITPRO})
        target_include_directories(foyer_plutonium SYSTEM PUBLIC
            $ENV{DEVKITPRO}/portlibs/switch/include
            $ENV{DEVKITPRO}/portlibs/switch/include/SDL2
            $ENV{DEVKITPRO}/portlibs/switch/include/freetype2)
    endif()
    set_target_properties(foyer_plutonium PROPERTIES CXX_STANDARD 17)

    # SDL2 family link surface for everything that pulls foyer_plutonium.
    target_link_libraries(foyer_plutonium PUBLIC
        SDL2_mixer SDL2_image SDL2_ttf SDL2_gfx SDL2
        EGL GLESv2 glapi drm_nouveau
        freetype harfbuzz png jpeg webp bz2 z
        opusfile opus modplug mpg123 vorbisidec
        m)
endif()

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------
add_subdirectory(shared)

# rcheevos is needed by both the player (full rc_client + memory model
# + frame triggers) and the browser (rc_hash for pre-play progress
# prefetch against RA's REST API). Declare once at top level.
include(${FOYER_CORES_DIR}/recipes/rcheevos.cmake)

if (BUILD_BROWSER)
    add_subdirectory(browser)
endif()

if (BUILD_PLAYER)
    # Resolve the list of cores to build. FOYER_CORES (semicolon list) wins;
    # fall back to single FOYER_CORE for legacy Player-* presets.
    if (FOYER_CORES)
        set(_foyer_player_cores ${FOYER_CORES})
    else()
        set(_foyer_player_cores ${FOYER_CORE})
    endif()

    foreach(_foyer_core IN LISTS _foyer_player_cores)
        set(FOYER_CORE ${_foyer_core})
        add_subdirectory(player player-${_foyer_core})
    endforeach()
endif()

message(STATUS "foyer ${FOYER_DISPLAY_VERSION}")
message(STATUS "  BUILD_BROWSER = ${BUILD_BROWSER}")
message(STATUS "  BUILD_PLAYER  = ${BUILD_PLAYER}")
if (BUILD_PLAYER)
    message(STATUS "  cores         = ${_foyer_player_cores}")
    message(STATUS "  recipes from  = ${FOYER_CORES_DIR}")
endif()
