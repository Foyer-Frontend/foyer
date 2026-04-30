# cores/prosystem.cmake — libretro ProSystem (Atari 7800) core build.
#
# Fetches the upstream source and compiles it as a static library named
# `core_prosystem`. The player nro links this directly so retro_run() and
# friends are resolved at link time.

include(FetchContent)

FetchContent_Declare(libretro_prosystem
    GIT_REPOSITORY https://github.com/libretro/prosystem-libretro.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_prosystem)

set(_PROSYS_DIR ${libretro_prosystem_SOURCE_DIR}/core)
set(_PROSYS_BUPBOOP_DIR ${libretro_prosystem_SOURCE_DIR}/bupboop)
set(_PROSYS_COMM_DIR ${libretro_prosystem_SOURCE_DIR}/libretro-common)

# Mirror the file list from upstream Makefile.common.
set(_PROSYS_CORE_SRC
    ${_PROSYS_DIR}/libretro.c
    ${_PROSYS_DIR}/Bios.c
    ${_PROSYS_DIR}/BupChip.c
    ${_PROSYS_DIR}/Cartridge.c
    ${_PROSYS_DIR}/Database.c
    ${_PROSYS_DIR}/Hash.c
    ${_PROSYS_DIR}/Maria.c
    ${_PROSYS_DIR}/Memory.c
    ${_PROSYS_DIR}/Palette.c
    ${_PROSYS_DIR}/Pokey.c
    ${_PROSYS_DIR}/ProSystem.c
    ${_PROSYS_DIR}/Region.c
    ${_PROSYS_DIR}/Riot.c
    ${_PROSYS_DIR}/Sally.c
    ${_PROSYS_DIR}/Tia.c
)

set(_PROSYS_BUPBOOP_SRC
    ${_PROSYS_BUPBOOP_DIR}/coretone/channel.c
    ${_PROSYS_BUPBOOP_DIR}/coretone/coretone.c
    ${_PROSYS_BUPBOOP_DIR}/coretone/music.c
    ${_PROSYS_BUPBOOP_DIR}/coretone/sample.c
)

set(_PROSYS_COMM_SRC
    ${_PROSYS_COMM_DIR}/compat/compat_posix_string.c
    ${_PROSYS_COMM_DIR}/compat/compat_strcasestr.c
    ${_PROSYS_COMM_DIR}/compat/compat_snprintf.c
    ${_PROSYS_COMM_DIR}/compat/compat_strl.c
    ${_PROSYS_COMM_DIR}/compat/fopen_utf8.c
    ${_PROSYS_COMM_DIR}/encodings/encoding_utf.c
    ${_PROSYS_COMM_DIR}/file/file_path.c
    ${_PROSYS_COMM_DIR}/file/file_path_io.c
    ${_PROSYS_COMM_DIR}/streams/file_stream.c
    ${_PROSYS_COMM_DIR}/streams/file_stream_transforms.c
    ${_PROSYS_COMM_DIR}/string/stdstring.c
    ${_PROSYS_COMM_DIR}/time/rtime.c
    ${_PROSYS_COMM_DIR}/vfs/vfs_implementation.c
)

foyer_core_static_library(
    NAME prosystem
    SOURCES
        ${_PROSYS_CORE_SRC}
        ${_PROSYS_BUPBOOP_SRC}
        ${_PROSYS_COMM_SRC}
    INCLUDE_DIRS
        ${_PROSYS_DIR}
        ${_PROSYS_BUPBOOP_DIR}
        ${_PROSYS_COMM_DIR}/include
    COMPILE_DEFS
        __LIBRETRO__=1
        SWITCH=1
        __SWITCH__=1
        HAVE_LIBNX=1
        HAVE_STDINT_H=1
        FRONTEND_SUPPORTS_RGB565=1
)
