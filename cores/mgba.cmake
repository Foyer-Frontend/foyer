# cores/mgba.cmake — libretro-mgba (Game Boy Advance) core build.
#
# mgba ships its own CMakeLists, but adding it as a subdirectory drags in
# heavy host-side deps (libpng, libzip, Qt, SDL, ...). Instead we mirror
# upstream's libretro-build/Makefile.common, which is the authoritative
# file list for the libretro core target. The Switch case in
# Makefile.libretro sets HAVE_VFS_FD=0 (so we use vfs-file.c) and adds
# `-DHAVE_LOCALE -D__SWITCH__ -DHAVE_LIBNX -DINLINE=inline`.
#
# foyer registers the mgba core only against the `gba` system in
# system_db.cpp; gambatte handles plain Game Boy. We still have to
# define M_CORE_GB and compile the GB/SM83 cores anyway: upstream's
# libretro.c references GB enums (GB_MODEL_AUTODETECT, GB_MODEL_AGB,
# GB_SIZE_WORKING_RAM) outside any #ifdef guard, so it won't compile
# without M_CORE_GB. Mirror upstream's Makefile.common file list.

include(FetchContent)

FetchContent_Declare(libretro_mgba
    GIT_REPOSITORY https://github.com/libretro/mgba.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)

# Avoid FetchContent_MakeAvailable / add_subdirectory: mgba's top-level
# CMakeLists pulls in deps and requires CMake compat versions we don't
# want to set globally. Populate-only gives us the source tree on disk.
FetchContent_GetProperties(libretro_mgba)
if (NOT libretro_mgba_POPULATED)
    FetchContent_Populate(libretro_mgba)
endif()

set(_MGBA_DIR ${libretro_mgba_SOURCE_DIR})
set(_MGBA_SRC ${_MGBA_DIR}/src)

# ---------------------------------------------------------------------------
# Source lists — mirror libretro-build/Makefile.common's SOURCES_C.
# ---------------------------------------------------------------------------

set(_MGBA_ARM_SRC
    ${_MGBA_SRC}/arm/arm.c
    ${_MGBA_SRC}/arm/decoder.c
    ${_MGBA_SRC}/arm/decoder-arm.c
    ${_MGBA_SRC}/arm/decoder-thumb.c
    ${_MGBA_SRC}/arm/isa-thumb.c
    ${_MGBA_SRC}/arm/isa-arm.c)

set(_MGBA_CORE_SRC
    ${_MGBA_SRC}/core/bitmap-cache.c
    ${_MGBA_SRC}/core/cache-set.c
    ${_MGBA_SRC}/core/cheats.c
    ${_MGBA_SRC}/core/config.c
    ${_MGBA_SRC}/core/core.c
    ${_MGBA_SRC}/core/interface.c
    ${_MGBA_SRC}/core/lockstep.c
    ${_MGBA_SRC}/core/log.c
    ${_MGBA_SRC}/core/map-cache.c
    ${_MGBA_SRC}/core/sync.c
    ${_MGBA_SRC}/core/thread.c
    ${_MGBA_SRC}/core/tile-cache.c
    ${_MGBA_SRC}/core/core-serialize.c
    ${_MGBA_SRC}/core/timing.c)

set(_MGBA_GB_SRC
    ${_MGBA_SRC}/gb/audio.c
    ${_MGBA_SRC}/gb/cheats.c
    ${_MGBA_SRC}/gb/core.c
    ${_MGBA_SRC}/gb/gb.c
    ${_MGBA_SRC}/gb/io.c
    ${_MGBA_SRC}/gb/mbc.c
    ${_MGBA_SRC}/gb/mbc/huc-3.c
    ${_MGBA_SRC}/gb/mbc/licensed.c
    ${_MGBA_SRC}/gb/mbc/mbc.c
    ${_MGBA_SRC}/gb/mbc/pocket-cam.c
    ${_MGBA_SRC}/gb/mbc/tama5.c
    ${_MGBA_SRC}/gb/mbc/unlicensed.c
    ${_MGBA_SRC}/gb/memory.c
    ${_MGBA_SRC}/gb/overrides.c
    ${_MGBA_SRC}/gb/renderers/cache-set.c
    ${_MGBA_SRC}/gb/renderers/software.c
    ${_MGBA_SRC}/gb/serialize.c
    ${_MGBA_SRC}/gb/sio.c
    ${_MGBA_SRC}/gb/timer.c
    ${_MGBA_SRC}/gb/video.c)

set(_MGBA_GBA_SRC
    ${_MGBA_SRC}/gba/audio.c
    ${_MGBA_SRC}/gba/bios.c
    ${_MGBA_SRC}/gba/cheats.c
    ${_MGBA_SRC}/gba/cheats/gameshark.c
    ${_MGBA_SRC}/gba/cheats/parv3.c
    ${_MGBA_SRC}/gba/cheats/codebreaker.c
    ${_MGBA_SRC}/gba/core.c
    ${_MGBA_SRC}/gba/dma.c
    ${_MGBA_SRC}/gba/gba.c
    ${_MGBA_SRC}/gba/cart/gpio.c
    ${_MGBA_SRC}/gba/cart/ereader.c
    ${_MGBA_SRC}/gba/cart/unlicensed.c
    ${_MGBA_SRC}/gba/hle-bios.c
    ${_MGBA_SRC}/gba/input.c
    ${_MGBA_SRC}/gba/io.c
    ${_MGBA_SRC}/gba/cart/matrix.c
    ${_MGBA_SRC}/gba/memory.c
    ${_MGBA_SRC}/gba/overrides.c
    ${_MGBA_SRC}/gba/renderers/cache-set.c
    ${_MGBA_SRC}/gba/renderers/common.c
    ${_MGBA_SRC}/gba/renderers/software-mode0.c
    ${_MGBA_SRC}/gba/renderers/software-obj.c
    ${_MGBA_SRC}/gba/renderers/software-bg.c
    ${_MGBA_SRC}/gba/renderers/video-software.c
    ${_MGBA_SRC}/gba/savedata.c
    ${_MGBA_SRC}/gba/serialize.c
    ${_MGBA_SRC}/gba/sio.c
    ${_MGBA_SRC}/gba/sio/gbp.c
    ${_MGBA_SRC}/gba/timer.c
    ${_MGBA_SRC}/gba/cart/vfame.c
    ${_MGBA_SRC}/gba/video.c)
# NOTE: src/gba/renderers/gl.c is excluded — it pulls OpenGL/GLES headers
# we don't have on Switch (foyer renders via deko3d). Upstream's
# Makefile.common omits it for the same reason.

set(_MGBA_SM83_SRC
    ${_MGBA_SRC}/sm83/isa-sm83.c
    ${_MGBA_SRC}/sm83/sm83.c)

set(_MGBA_RETRO_SRC
    ${_MGBA_SRC}/platform/libretro/memory.c
    ${_MGBA_SRC}/platform/libretro/libretro.c)

set(_MGBA_INIH_SRC
    ${_MGBA_SRC}/third-party/inih/ini.c)

set(_MGBA_UTIL_SRC
    ${_MGBA_SRC}/util/audio-buffer.c
    ${_MGBA_SRC}/util/audio-resampler.c
    ${_MGBA_SRC}/util/interpolator.c
    ${_MGBA_SRC}/util/circle-buffer.c
    ${_MGBA_SRC}/util/configuration.c
    ${_MGBA_SRC}/util/formatting.c
    ${_MGBA_SRC}/util/gbk-table.c
    ${_MGBA_SRC}/util/geometry.c
    ${_MGBA_SRC}/util/hash.c
    ${_MGBA_SRC}/util/image.c
    ${_MGBA_SRC}/util/md5.c
    ${_MGBA_SRC}/util/sha1.c
    ${_MGBA_SRC}/util/patch.c
    ${_MGBA_SRC}/util/patch-ips.c
    ${_MGBA_SRC}/util/patch-ups.c
    ${_MGBA_SRC}/util/string.c
    ${_MGBA_SRC}/util/table.c
    ${_MGBA_SRC}/util/vector.c
    ${_MGBA_SRC}/util/vfs.c
    ${_MGBA_SRC}/util/vfs/vfs-mem.c
    ${_MGBA_SRC}/util/vfs/vfs-file.c   # needed under ENABLE_VFS_FILE
    ${_MGBA_SRC}/util/crc32.c)

# ---------------------------------------------------------------------------
# Build the core.
# ---------------------------------------------------------------------------
foyer_core_static_library(
    NAME mgba
    SOURCES
        ${_MGBA_ARM_SRC}
        ${_MGBA_CORE_SRC}
        ${_MGBA_GB_SRC}
        ${_MGBA_GBA_SRC}
        ${_MGBA_SM83_SRC}
        ${_MGBA_RETRO_SRC}
        ${_MGBA_INIH_SRC}
        ${_MGBA_UTIL_SRC}
    INCLUDE_DIRS
        ${_MGBA_DIR}/include
        ${_MGBA_SRC}
        ${_MGBA_SRC}/arm
        ${_MGBA_SRC}/platform/libretro
        ${ZLIB_INCLUDE_DIRS}
    COMPILE_DEFS
        # _GNU_SOURCE pulls strtof_l/strtod_l/uselocale prototypes out of
        # newlib's stdlib.h/locale.h (gated behind __GNU_VISIBLE).
        _GNU_SOURCE=1
        # Upstream libretro target defines (CMakeLists.txt:1015 +
        # libretro-build/Makefile.common). HAVE_VFS_FD=0 path on Switch
        # → use ENABLE_VFS_FILE / vfs-file.c instead of mmap-backed fd.
        __LIBRETRO__=1
        MINIMAL_CORE=2
        M_CORE_GBA=1
        M_CORE_GB=1              # required for libretro.c to compile
        ENABLE_VFS=1
        ENABLE_DIRECTORIES=1
        ENABLE_VFS_FILE=1
        DISABLE_THREADING=1
        MGBA_STANDALONE=1
        COLOR_16_BIT=1
        COLOR_5_6_5=1
        FRONTEND_SUPPORTS_RGB565=1
        RESAMPLE_LIBRARY=2
        # Inline keyword: upstream's Switch row pins INLINE=inline.
        INLINE=inline
        # Newlib / libnx feature flags.
        HAVE_STDINT_H=1
        HAVE_INTTYPES_H=1
        HAVE_LOCALE=1            # devkitA64 newlib has uselocale et al.
        HAVE_LOCALTIME_R=1
        HAVE_STRTOF_L=1
        HAVE_STRDUP=1
        HAVE_STRNDUP=1
        HAVE_CRC32=1             # we link zlib at the player level
        # Limits.
        PATH_MAX=4096
        SSIZE_MAX=2147483648
        # M_PI from <math.h> isn't visible without _GNU_SOURCE on newlib;
        # mirror upstream's explicit fallback.
        "M_PI=3.14159265358979323846"
        # Switch / libnx target identification (also wards off
        # libretro-common's mmap.h sys/mman.h include).
        SWITCH=1
        __SWITCH__=1
        HAVE_LIBNX=1
)

# zlib is provided by devkitPro's portlibs; libretro frontend already
# links it at the executable level, but the core also calls crc32() so
# resolve it via the imported target's interface include + libs.
if (TARGET ZLIB::ZLIB)
    target_link_libraries(core_mgba PUBLIC ZLIB::ZLIB)
endif()
