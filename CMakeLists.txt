cmake_minimum_required(VERSION 3.21)

project(foyer
    VERSION 0.5.7
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_BROWSER "Build the foyer browser nro"     ON)
option(BUILD_PLAYER  "Build per-core player nros"      OFF)
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
# Subprojects
# ---------------------------------------------------------------------------
add_subdirectory(shared)

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

    # rcheevos is shared by every player binary — declare it once so we don't
    # collide on target name when looping the player subdir.
    include(${FOYER_CORES_DIR}/recipes/rcheevos.cmake)

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
