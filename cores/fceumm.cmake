# cores/fceumm.cmake — libretro-fceumm (NES) core build.
#
# Fetches the upstream source and compiles it as a static library named
# `core_fceumm`. The player nro links this directly so retro_run() and
# friends are resolved at link time.

include(FetchContent)

FetchContent_Declare(libretro_fceumm
    GIT_REPOSITORY https://github.com/libretro/libretro-fceumm.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_fceumm)

set(_FCEUMM_DIR ${libretro_fceumm_SOURCE_DIR}/src)
set(_FCEUMM_LIBRETRO_DIR ${_FCEUMM_DIR}/drivers/libretro)
set(_FCEUMM_COMM_DIR ${_FCEUMM_LIBRETRO_DIR}/libretro-common)

file(GLOB _FCEUMM_BOARD_SRC "${_FCEUMM_DIR}/boards/*.c")
file(GLOB _FCEUMM_INPUT_SRC "${_FCEUMM_DIR}/input/*.c")

# Mirror the file list from upstream Makefile.common (non-STATIC_LINKING build
# so we get the libretro-common compat helpers compiled in too).
set(_FCEUMM_CORE_SRC
    ${_FCEUMM_DIR}/cart.c
    ${_FCEUMM_DIR}/cheat.c
    ${_FCEUMM_DIR}/crc32.c
    ${_FCEUMM_DIR}/fceu-endian.c
    ${_FCEUMM_DIR}/fceu-memory.c
    ${_FCEUMM_DIR}/fceu.c
    ${_FCEUMM_DIR}/fds.c
    ${_FCEUMM_DIR}/fds_apu.c
    ${_FCEUMM_DIR}/file.c
    ${_FCEUMM_DIR}/filter.c
    ${_FCEUMM_DIR}/general.c
    ${_FCEUMM_DIR}/input.c
    ${_FCEUMM_DIR}/md5.c
    ${_FCEUMM_DIR}/nsf.c
    ${_FCEUMM_DIR}/palette.c
    ${_FCEUMM_DIR}/ppu.c
    ${_FCEUMM_DIR}/sound.c
    ${_FCEUMM_DIR}/state.c
    ${_FCEUMM_DIR}/video.c
    ${_FCEUMM_DIR}/vsuni.c
    ${_FCEUMM_DIR}/ines.c
    ${_FCEUMM_DIR}/unif.c
    ${_FCEUMM_DIR}/x6502.c
    ${_FCEUMM_LIBRETRO_DIR}/libretro.c
    ${_FCEUMM_LIBRETRO_DIR}/libretro_dipswitch.c
)

set(_FCEUMM_COMM_SRC
    ${_FCEUMM_COMM_DIR}/compat/compat_posix_string.c
    ${_FCEUMM_COMM_DIR}/compat/compat_snprintf.c
    ${_FCEUMM_COMM_DIR}/compat/compat_strcasestr.c
    ${_FCEUMM_COMM_DIR}/compat/compat_strl.c
    ${_FCEUMM_COMM_DIR}/compat/fopen_utf8.c
    ${_FCEUMM_COMM_DIR}/encodings/encoding_utf.c
    ${_FCEUMM_COMM_DIR}/file/file_path.c
    ${_FCEUMM_COMM_DIR}/file/file_path_io.c
    ${_FCEUMM_COMM_DIR}/streams/file_stream.c
    ${_FCEUMM_COMM_DIR}/streams/file_stream_transforms.c
    ${_FCEUMM_COMM_DIR}/streams/memory_stream.c
    ${_FCEUMM_COMM_DIR}/string/stdstring.c
    ${_FCEUMM_COMM_DIR}/time/rtime.c
    ${_FCEUMM_COMM_DIR}/vfs/vfs_implementation.c
)

foyer_core_static_library(
    NAME fceumm
    SOURCES
        ${_FCEUMM_BOARD_SRC}
        ${_FCEUMM_INPUT_SRC}
        ${_FCEUMM_CORE_SRC}
        ${_FCEUMM_COMM_SRC}
    INCLUDE_DIRS
        ${_FCEUMM_DIR}
        ${_FCEUMM_DIR}/boards
        ${_FCEUMM_DIR}/input
        ${_FCEUMM_LIBRETRO_DIR}
        ${_FCEUMM_COMM_DIR}/include
    COMPILE_DEFS
        __LIBRETRO__=1
        PATH_MAX=1024
        FCEU_VERSION_NUMERIC=9813
        FRONTEND_SUPPORTS_RGB565=1
        HAVE_ASPRINTF=1
)
