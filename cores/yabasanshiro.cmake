# cores/yabasanshiro.cmake — libretro-yabause (Sega Saturn) core build.
#
# Fetches the upstream libretro/yabause source and compiles it as a static
# library named `core_yabasanshiro`. We follow the upstream `libnx` Makefile
# target (STATIC_LINKING=1, vidsoft software renderer, no GL, no SH-2 dynarec)
# so the build only needs libnx + devkitA64 — no Mesa/EGL/GLES headers, no
# bundled assembly.
#
# Notes:
#   * library_name in upstream libretro.c reports "Yabause" (not Yabasanshiro).
#     We label the foyer recipe `yabasanshiro` to match the foyer system slug
#     for Sega Saturn; the underlying core is the libretro/yabause fork.
#   * CHD / extra zlib paths are disabled to keep the source list tight; the
#     core still loads bin/cue + iso through cd-libretro.c.
#   * Musashi M68000 core is preferred (HAVE_MUSASHI=1); the alternate c68k
#     path requires running an upstream codegen tool at configure time.

include(FetchContent)

FetchContent_Declare(libretro_yabause
    GIT_REPOSITORY https://github.com/libretro/yabause.git
    GIT_TAG        7cb15b8f9eea5a6fa7cae34468a6989522bcba75
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_yabause)

set(_YABA_DIR          ${libretro_yabause_SOURCE_DIR}/yabause/src)
set(_YABA_LIBRETRO_DIR ${_YABA_DIR}/libretro)
set(_YABA_COMM_DIR     ${_YABA_LIBRETRO_DIR}/libretro-common)

# Mirror Makefile.common's SOURCES_C list with HAVE_GRIFFIN=0, HAVE_GL=0,
# HAVE_MUSASHI=1, HAVE_THREADS=1, USE_SCSP2=0, ENABLE_ZLIB=0, ENABLE_CHD=0,
# DYNAREC=0, USE_PLAY_JIT=0.
set(_YABA_CORE_SRC
    ${_YABA_DIR}/osdcore.c
    ${_YABA_DIR}/bios.c
    ${_YABA_DIR}/cd-libretro.c
    ${_YABA_DIR}/cd_drive.c
    ${_YABA_DIR}/cheat.c
    ${_YABA_DIR}/coffelf.c
    ${_YABA_DIR}/cs0.c
    ${_YABA_DIR}/cs1.c
    ${_YABA_DIR}/cs2.c
    ${_YABA_DIR}/debug.c
    ${_YABA_DIR}/error.c
    ${_YABA_DIR}/gameinfo.c
    ${_YABA_DIR}/japmodem.c
    ${_YABA_DIR}/m68kcore.c
    ${_YABA_DIR}/memory.c
    ${_YABA_DIR}/movie.c
    ${_YABA_DIR}/mpeg_card.c
    ${_YABA_DIR}/netlink.c
    ${_YABA_DIR}/peripheral.c
    ${_YABA_DIR}/profile.c
    ${_YABA_DIR}/scspdsp.c
    ${_YABA_DIR}/scu.c
    ${_YABA_DIR}/sh2cache.c
    ${_YABA_DIR}/sh2core.c
    ${_YABA_DIR}/sh2d.c
    ${_YABA_DIR}/sh2idle.c
    ${_YABA_DIR}/sh2int.c
    ${_YABA_DIR}/sh2trace.c
    ${_YABA_DIR}/sh7034.c
    ${_YABA_DIR}/smpc.c
    ${_YABA_DIR}/ygr.c
    ${_YABA_DIR}/scsp.c
    # Musashi (M68000) — pre-generated sources are checked into upstream.
    ${_YABA_DIR}/musashi/m68kcpu.c
    ${_YABA_DIR}/musashi/m68kops.c
    ${_YABA_DIR}/musashi/m68kopnz.c
    ${_YABA_DIR}/musashi/m68kopac.c
    ${_YABA_DIR}/musashi/m68kopdm.c
    ${_YABA_DIR}/m68kmusashi.c
    # Software renderer + glue (HAVE_GRIFFIN=0 path).
    ${_YABA_DIR}/snddummy.c
    ${_YABA_DIR}/vdp1.c
    ${_YABA_DIR}/vdp2.c
    ${_YABA_DIR}/vidshared.c
    ${_YABA_DIR}/vidsoft.c
    ${_YABA_DIR}/titan/titan.c
    ${_YABA_DIR}/yabause.c
    ${_YABA_DIR}/thr-rthreads.c
    ${_YABA_LIBRETRO_DIR}/libretro.c
)

# libretro-common helpers (mirrors the !STATIC_LINKING block in
# Makefile.common; we *do* compile them — STATIC_LINKING here just means the
# core is delivered as a .a, not that frontend already provides them).
set(_YABA_COMM_SRC
    ${_YABA_COMM_DIR}/streams/file_stream.c
    ${_YABA_COMM_DIR}/streams/file_stream_transforms.c
    ${_YABA_COMM_DIR}/compat/fopen_utf8.c
    ${_YABA_COMM_DIR}/compat/compat_posix_string.c
    ${_YABA_COMM_DIR}/compat/compat_strl.c
    ${_YABA_COMM_DIR}/compat/compat_strcasestr.c
    ${_YABA_COMM_DIR}/encodings/encoding_utf.c
    ${_YABA_COMM_DIR}/file/file_path.c
    ${_YABA_COMM_DIR}/string/stdstring.c
    ${_YABA_COMM_DIR}/time/rtime.c
    ${_YABA_COMM_DIR}/rthreads/rthreads.c
    ${_YABA_COMM_DIR}/vfs/vfs_implementation.c
)

foyer_core_static_library(
    NAME yabasanshiro
    SOURCES
        ${_YABA_CORE_SRC}
        ${_YABA_COMM_SRC}
    INCLUDE_DIRS
        ${_YABA_LIBRETRO_DIR}
        ${_YABA_DIR}
        ${_YABA_COMM_DIR}/include
        ${_YABA_DIR}/musashi
        ${ZLIB_INCLUDE_DIRS}
    COMPILE_DEFS
        __LIBRETRO__=1
        SWITCH=1
        __SWITCH__=1
        HAVE_LIBNX=1
        HAVE_STDINT_H=1
        HAVE_SYS_TIME_H=1
        HAVE_GETTIMEOFDAY=1
        HAVE_THREADS=1
        HAVE_MUSASHI=1
        HAVE_BUILTIN_BSWAP16=1
        HAVE_BUILTIN_BSWAP32=1
        HAVE_C99_VARIADIC_MACROS=1
        HAVE_FLOORF=1
        IMPROVED_SAVESTATES
        NO_CLI
        USE_16BPP=1
        USE_RGB_565=1
        VERSION="0.9.15"
        RARCH_INTERNAL
        # Ensure netlink/japmodem/etc don't try to pull in BSD sockets paths
        # we don't have on libnx.
        HAVE_NETWORK_STUB=1
        # Older libretro-common in this fork references DIR_MAX_LENGTH but
        # only NAME_MAX_LENGTH / PATH_MAX_LENGTH are defined in the vendored
        # retro_miscellaneous.h. Provide a sane default that matches modern
        # libretro-common's value.
        DIR_MAX_LENGTH=4096
)
