# cores/race.cmake — libretro RACE (Neo Geo Pocket / Color) core build.
#
# Fetches the upstream source and compiles it as a static library named
# `core_race`. The player nro links this directly so retro_run() and
# friends are resolved at link time.

include(FetchContent)

FetchContent_Declare(libretro_race
    GIT_REPOSITORY https://github.com/libretro/RACE.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_race)

set(_RACE_DIR ${libretro_race_SOURCE_DIR})
set(_RACE_LIBRETRO_DIR ${_RACE_DIR}/libretro)
set(_RACE_COMM_DIR ${_RACE_DIR}/libretro-common)

# Mirror the file list from upstream Makefile.common (non-STATIC_LINKING build,
# CZ80 path — the portable C Z80 core).
set(_RACE_CORE_SRC
    ${_RACE_DIR}/tlcs900h.c
    ${_RACE_DIR}/graphics.c
    ${_RACE_DIR}/main.c
    ${_RACE_DIR}/flash.c
    ${_RACE_DIR}/input.c
    ${_RACE_DIR}/race-memory.c
    ${_RACE_DIR}/ngpBios.c
    ${_RACE_DIR}/state.c
    ${_RACE_DIR}/sound.c
    ${_RACE_DIR}/neopopsound.c
    ${_RACE_DIR}/cz80.c
    ${_RACE_DIR}/cz80_support.c
    ${_RACE_LIBRETRO_DIR}/libretro.c
    ${_RACE_LIBRETRO_DIR}/log.c
)

set(_RACE_COMM_SRC
    ${_RACE_COMM_DIR}/compat/compat_posix_string.c
    ${_RACE_COMM_DIR}/compat/compat_strcasestr.c
    ${_RACE_COMM_DIR}/compat/compat_strl.c
    ${_RACE_COMM_DIR}/compat/fopen_utf8.c
    ${_RACE_COMM_DIR}/encodings/encoding_utf.c
    ${_RACE_COMM_DIR}/file/file_path.c
    ${_RACE_COMM_DIR}/file/file_path_io.c
    ${_RACE_COMM_DIR}/streams/file_stream.c
    ${_RACE_COMM_DIR}/streams/file_stream_transforms.c
    ${_RACE_COMM_DIR}/string/stdstring.c
    ${_RACE_COMM_DIR}/time/rtime.c
    ${_RACE_COMM_DIR}/vfs/vfs_implementation.c
)

foyer_core_static_library(
    NAME race
    SOURCES
        ${_RACE_CORE_SRC}
        ${_RACE_COMM_SRC}
    INCLUDE_DIRS
        ${_RACE_DIR}
        ${_RACE_LIBRETRO_DIR}
        ${_RACE_COMM_DIR}/include
    COMPILE_DEFS
        __LIBRETRO__=1
        SWITCH=1
        __SWITCH__=1
        HAVE_LIBNX=1
        HAVE_STDINT_H=1
        FRONTEND_SUPPORTS_RGB565=1
        CZ80=1
        _MAX_PATH=2048
)
