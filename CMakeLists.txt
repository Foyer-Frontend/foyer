cmake_minimum_required(VERSION 3.21)

project(foyer
    VERSION 0.1.0
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_BROWSER "Build the foyer browser nro"             ON)
option(BUILD_PLAYER  "Build a foyer-<core> player nro"         OFF)
set(FOYER_CORE       "" CACHE STRING
    "Libretro core to bake into the player nro (e.g. fceumm). \
Required when BUILD_PLAYER=ON.")

if (BUILD_PLAYER AND NOT FOYER_CORE)
    message(FATAL_ERROR
        "BUILD_PLAYER=ON requires FOYER_CORE=<core_name>")
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
    add_subdirectory(player)
endif()

message(STATUS "foyer ${FOYER_DISPLAY_VERSION}")
message(STATUS "  BUILD_BROWSER = ${BUILD_BROWSER}")
message(STATUS "  BUILD_PLAYER  = ${BUILD_PLAYER}")
if (BUILD_PLAYER)
    message(STATUS "  FOYER_CORE    = ${FOYER_CORE}")
endif()
