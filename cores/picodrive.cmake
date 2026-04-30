# cores/picodrive.cmake — libretro picodrive (Sega MD/Genesis/SMS/GG/32X/MCD/Pico).
#
# An alternative to genesis_plus_gx that adds 32X + Sega CD + Pico support.
# Mirrors upstream Makefile.libretro's `platform=libnx` block, which on
# aarch64 yields:
#
#   - Cyclone 68K is ARM32-only, so we use FAME (cpu/fame/famec.c) for 68K.
#   - DrZ80 is ARM32-only, so we use CZ80 (cpu/cz80/cz80.c) for Z80.
#   - SH2 dynarec is enabled (use_sh2drc=1) — cpu/sh2/compiler.c #includes
#     cpu/drc/emit_arm64.c via DRC_SH2 + the recipe's aarch64 detection.
#   - SVP DRC (svp/compiler.c + stub_arm.S) is gated to ARM32 in upstream's
#     defaults; we keep the plain SVP interp (svp.c + memory.c + ssp16.c).
#   - libchdr (with bundled lzma + zstd) for CHD CD-ROM image support.
#   - libretro-common compat layer instead of devkitPro's; libretro VFS on.
#   - PLATFORM_TREMOR=1: bundled libtremor (integer Vorbis decoder) so we
#     don't need to chase the system libvorbis.
#   - PLATFORM_ZLIB=1: in-tree zlib subset (some headers we reuse from
#     devkitPro-z but the bundled .c paths bypass header-vs-link mismatches).
#   - libpicofe is included only for `plat.h` (one struct/macro picked up
#     by libretro.c) — no compiled libpicofe sources end up in the .a.
#
# The libretro entry point (platform/libretro/libretro.c) `#define _GNU_SOURCE 1`
# pulls in `mremap` from libretro-common's memmap.h — that header skips
# `<sys/mman.h>` whenever SWITCH or HAVE_LIBNX is defined, so we just inherit
# the standard libnx-style behaviour shared with genesis_plus_gx.

enable_language(ASM)

include(FetchContent)

FetchContent_Declare(libretro_picodrive
    GIT_REPOSITORY https://github.com/libretro/picodrive.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
# Submodules: cpu/cyclone, pico/cd/libchdr, pico/sound/emu2413,
# platform/common/dr_libs, platform/libpicofe.
set(FETCHCONTENT_UPDATES_DISCONNECTED_LIBRETRO_PICODRIVE ON)
FetchContent_GetProperties(libretro_picodrive)
if (NOT libretro_picodrive_POPULATED)
    FetchContent_Populate(libretro_picodrive)
    # Submodules — the FetchContent default doesn't recurse.
    execute_process(
        COMMAND git submodule update --init --recursive --depth=1
        WORKING_DIRECTORY ${libretro_picodrive_SOURCE_DIR}
        RESULT_VARIABLE _pd_sm_rc)
    if (NOT _pd_sm_rc EQUAL 0)
        message(FATAL_ERROR "picodrive submodule init failed (rc=${_pd_sm_rc})")
    endif()
endif()

set(_PD       ${libretro_picodrive_SOURCE_DIR})
set(_PD_LR    ${_PD}/platform/libretro)
set(_PD_LRC   ${_PD_LR}/libretro-common)
set(_PD_COMM  ${_PD}/platform/common)
set(_PD_CPU   ${_PD}/cpu)
set(_PD_CHDR  ${_PD}/pico/cd/libchdr)
set(_PD_LZMA  ${_PD_CHDR}/deps/lzma-24.05)
set(_PD_ZSTD  ${_PD_CHDR}/deps/zstd-1.5.6/lib)

# ---------------------------------------------------------------------------
# Pico core (matches platform/common/common.mak SRCS_COMMON, aarch64 path).
# ---------------------------------------------------------------------------
set(_PD_PICO_SRC
    ${_PD}/pico/pico.c
    ${_PD}/pico/cart.c
    ${_PD}/pico/memory.c
    ${_PD}/pico/state.c
    ${_PD}/pico/sek.c
    ${_PD}/pico/z80if.c
    ${_PD}/pico/videoport.c
    ${_PD}/pico/draw2.c
    ${_PD}/pico/draw.c
    ${_PD}/pico/mode4.c
    ${_PD}/pico/misc.c
    ${_PD}/pico/eeprom.c
    ${_PD}/pico/patch.c
    ${_PD}/pico/debug.c
    ${_PD}/pico/media.c
    ${_PD}/pico/sms.c
)

# Mega CD
set(_PD_CD_SRC
    ${_PD}/pico/cd/mcd.c
    ${_PD}/pico/cd/memory.c
    ${_PD}/pico/cd/sek.c
    ${_PD}/pico/cd/cdc.c
    ${_PD}/pico/cd/cdd.c
    ${_PD}/pico/cd/cd_image.c
    ${_PD}/pico/cd/cd_parse.c
    ${_PD}/pico/cd/gfx.c
    ${_PD}/pico/cd/gfx_dma.c
    ${_PD}/pico/cd/misc.c
    ${_PD}/pico/cd/pcm.c
    ${_PD}/pico/cd/megasd.c
)

# 32X
set(_PD_32X_SRC
    ${_PD}/pico/32x/32x.c
    ${_PD}/pico/32x/memory.c
    ${_PD}/pico/32x/draw.c
    ${_PD}/pico/32x/sh2soc.c
    ${_PD}/pico/32x/pwm.c
)

# Pico (Sega Pico tablet)
set(_PD_PICOPICO_SRC
    ${_PD}/pico/pico/pico.c
    ${_PD}/pico/pico/memory.c
    ${_PD}/pico/pico/xpcm.c
)

# carthw + SVP (interp only on aarch64 — no svp/compiler.c, no stub_arm.S).
set(_PD_CARTHW_SRC
    ${_PD}/pico/carthw/carthw.c
    ${_PD}/pico/carthw/eeprom_spi.c
    ${_PD}/pico/carthw/svp/svp.c
    ${_PD}/pico/carthw/svp/memory.c
    ${_PD}/pico/carthw/svp/ssp16.c
)

# Sound
set(_PD_SOUND_SRC
    ${_PD}/pico/sound/sound.c
    ${_PD}/pico/sound/resampler.c
    ${_PD}/pico/sound/sn76496.c
    ${_PD}/pico/sound/ym2612.c
    ${_PD}/pico/sound/ym2413.c
    ${_PD}/pico/sound/vgm.c
    ${_PD}/pico/sound/mix.c
)

# ---------------------------------------------------------------------------
# CPU cores — aarch64 fallback set:
#   M68K = FAME (famec.c), Z80 = CZ80, SH2 = MAME interp + DRC compiler.
# ---------------------------------------------------------------------------
set(_PD_CPU_SRC
    ${_PD_CPU}/fame/famec.c
    ${_PD_CPU}/cz80/cz80.c
    ${_PD_CPU}/drc/cmn.c
    ${_PD_CPU}/sh2/sh2.c
    ${_PD_CPU}/sh2/compiler.c
    ${_PD_CPU}/sh2/mame/sh2pico.c
)
# cpu/sh2/compiler.c #includes cpu/drc/emit_arm64.c at preprocess time
# (Makefile dep line: cpu/sh2/compiler.o : cpu/drc/emit_arm64.c).

# ---------------------------------------------------------------------------
# libretro frontend glue + libretro-common compat shims (mirrors Makefile
# PLATFORM=libretro, USE_LIBRETRO_VFS=1, STATIC_LINKING=0 path).
# ---------------------------------------------------------------------------
set(_PD_LR_SRC
    ${_PD_LR}/libretro.c
    ${_PD_LRC}/formats/png/rpng.c
    ${_PD_LRC}/streams/trans_stream.c
    ${_PD_LRC}/streams/trans_stream_pipe.c
    ${_PD_LRC}/streams/trans_stream_zlib.c
    ${_PD_LRC}/file/file_path_io.c
    ${_PD_LRC}/file/file_path.c
    ${_PD_LRC}/vfs/vfs_implementation.c
    ${_PD_LRC}/time/rtime.c
    ${_PD_LRC}/string/stdstring.c
    ${_PD_LRC}/compat/compat_strcasestr.c
    ${_PD_LRC}/encodings/encoding_utf.c
    ${_PD_LRC}/compat/compat_strl.c
    # USE_LIBRETRO_VFS=1
    ${_PD_LRC}/compat/compat_posix_string.c
    ${_PD_LRC}/compat/fopen_utf8.c
    ${_PD_LRC}/streams/file_stream.c
    ${_PD_LRC}/streams/file_stream_transforms.c
    ${_PD_LRC}/memmap/memmap.c
)

# ---------------------------------------------------------------------------
# platform/common — picodrive's audio decoder layer (mp3 + ogg via tremor).
# Used by Mega CD / MegaSD audio tracks. PLATFORM_TREMOR=1 bundles libtremor.
# ---------------------------------------------------------------------------
set(_PD_COMM_SRC
    ${_PD_COMM}/mp3.c
    ${_PD_COMM}/mp3_sync.c
    ${_PD_COMM}/mp3_drmp3.c
    ${_PD_COMM}/ogg.c
)

# Tremor (integer Vorbis) — bundled, picked up when PLATFORM_TREMOR=1.
set(_PD_TREMOR ${_PD_COMM}/tremor)
set(_PD_TREMOR_SRC
    ${_PD_TREMOR}/block.c
    ${_PD_TREMOR}/codebook.c
    ${_PD_TREMOR}/floor0.c
    ${_PD_TREMOR}/floor1.c
    ${_PD_TREMOR}/info.c
    ${_PD_TREMOR}/mapping0.c
    ${_PD_TREMOR}/mdct.c
    ${_PD_TREMOR}/registry.c
    ${_PD_TREMOR}/res012.c
    ${_PD_TREMOR}/sharedbook.c
    ${_PD_TREMOR}/synthesis.c
    ${_PD_TREMOR}/window.c
    ${_PD_TREMOR}/vorbisfile.c
    ${_PD_TREMOR}/framing.c
    ${_PD_TREMOR}/bitwise.c
)

# ---------------------------------------------------------------------------
# libchdr + bundled zstd / lzma (CHD CD-ROM image reader for Mega CD).
# ---------------------------------------------------------------------------
set(_PD_CHDR_SRC
    ${_PD_CHDR}/src/libchdr_chd.c
    ${_PD_CHDR}/src/libchdr_cdrom.c
    ${_PD_CHDR}/src/libchdr_flac.c
    ${_PD_CHDR}/src/libchdr_bitstream.c
    ${_PD_CHDR}/src/libchdr_huffman.c
)

set(_PD_LZMA_SRC
    ${_PD_LZMA}/src/CpuArch.c
    ${_PD_LZMA}/src/Alloc.c
    ${_PD_LZMA}/src/LzmaEnc.c
    ${_PD_LZMA}/src/Sort.c
    ${_PD_LZMA}/src/LzmaDec.c
    ${_PD_LZMA}/src/LzFind.c
    ${_PD_LZMA}/src/Delta.c
)

set(_PD_ZSTD_SRC
    ${_PD_ZSTD}/common/entropy_common.c
    ${_PD_ZSTD}/common/error_private.c
    ${_PD_ZSTD}/common/fse_decompress.c
    ${_PD_ZSTD}/common/xxhash.c
    ${_PD_ZSTD}/common/zstd_common.c
    ${_PD_ZSTD}/decompress/huf_decompress.c
    ${_PD_ZSTD}/decompress/zstd_ddict.c
    ${_PD_ZSTD}/decompress/zstd_decompress_block.c
    ${_PD_ZSTD}/decompress/zstd_decompress.c
)

# ---------------------------------------------------------------------------
# Bundled zlib (PLATFORM_ZLIB=1). picodrive's includes are local so the
# bundled headers win over devkitPro's; we link the bundled .c files too.
# ---------------------------------------------------------------------------
set(_PD_ZLIB_SRC
    ${_PD}/zlib/gzio.c
    ${_PD}/zlib/inffast.c
    ${_PD}/zlib/inflate.c
    ${_PD}/zlib/inftrees.c
    ${_PD}/zlib/trees.c
    ${_PD}/zlib/deflate.c
    ${_PD}/zlib/crc32.c
    ${_PD}/zlib/adler32.c
    ${_PD}/zlib/zutil.c
    ${_PD}/zlib/compress.c
    ${_PD}/zlib/uncompr.c
)

# unzip (zip-loaded ROM support).
set(_PD_UNZIP_SRC ${_PD}/unzip/unzip.c)

# ---------------------------------------------------------------------------
# Build the static library.
# ---------------------------------------------------------------------------
set(_PD_TARGET core_picodrive)

add_library(${_PD_TARGET} STATIC
    ${_PD_PICO_SRC}
    ${_PD_CD_SRC}
    ${_PD_32X_SRC}
    ${_PD_PICOPICO_SRC}
    ${_PD_CARTHW_SRC}
    ${_PD_SOUND_SRC}
    ${_PD_CPU_SRC}
    ${_PD_LR_SRC}
    ${_PD_COMM_SRC}
    ${_PD_TREMOR_SRC}
    ${_PD_CHDR_SRC}
    ${_PD_LZMA_SRC}
    ${_PD_ZSTD_SRC}
    ${_PD_ZLIB_SRC}
    ${_PD_UNZIP_SRC}
)

target_include_directories(${_PD_TARGET} PUBLIC
    # The switch/ shim ships local mman.h; libretro.c only pulls
    # libretro-common's memmap.h header (which skips <sys/mman.h> on
    # SWITCH/HAVE_LIBNX), but compiler.c's emit_arm64 path uses cacheflush
    # via the libnx headers directly — so keep the dir near the front.
    ${_PD_LR}/switch
    # libchdr's chd.h must come before any libretro-common chd.h to avoid
    # an RFILE-vs-FILE* signature mismatch.
    ${_PD_CHDR}/include
    ${_PD_LZMA}/include
    ${_PD_ZSTD}
    ${_PD}
    ${_PD}/pico
    ${_PD}/zlib
    ${_PD_LRC}/include
    ${_PD_LRC}/include/compat
    ${_PD_LRC}/include/encodings
    ${_PD_LRC}/include/formats
    ${_PD_LRC}/include/streams
    ${_PD_LRC}/include/string
    ${_PD_LRC}/include/vfs
    # libretro.c does `#include <platform/libpicofe/plat.h>` for one PXMAKE
    # macro. We don't compile any libpicofe .c files.
    ${_PD}
    ${_PD_TREMOR}
)

target_compile_definitions(${_PD_TARGET} PRIVATE
    # libretro / libnx baseline
    __LIBRETRO__=1
    HAVE_LIBRETRO=1
    SWITCH=1
    __SWITCH__=1
    HAVE_LIBNX=1
    HAVE_STDINT_H=1

    # Toolchain identifiers Picodrive's headers branch on.
    ARM=1
    __aarch64__=1

    # Picodrive build switches (mirror Makefile.libretro libnx + Makefile
    # ARCH-default block when ARCH=aarch64).
    PLATFORM=libretro
    NO_CONFIG_MAK=1
    USE_LIBRETRO_VFS=1
    USE_LIBCHDR=1
    HAVE_ZLIB=1
    USE_TREMOR=1
    PLATFORM_ZLIB=1
    PLATFORM_TREMOR=1
    LSB_FIRST=1

    # FAME 68K + CZ80 + SH2 DRC (no Cyclone, no DrZ80 on aarch64).
    EMU_F68K=1
    _USE_CZ80=1
    DRC_SH2=1

    # libchdr / lzma / zstd tunables (single-thread, decompress only).
    Z7_ST=1
    ZSTD_DISABLE_ASM=1

    # libretro.c uses `#define _GNU_SOURCE 1` for mremap; harmless to also
    # set it project-wide so shared headers stay consistent.
    _GNU_SOURCE=1

    # Quiet revision string (Makefile.libretro normally derives from git).
    "REVISION=\"foyer\""

    # NB: don't override INLINE — picodrive headers (retro_inline.h,
    # ym2612.c, emu2413.c) all use `static INLINE foo()` which expands to
    # `static inline foo()`. Forcing INLINE to "static inline" would
    # double-emit the storage class and break compilation.

    NDEBUG=1
)

target_compile_options(${_PD_TARGET} PRIVATE
    -w
    -fno-strict-aliasing
    -ffast-math
    -fomit-frame-pointer
    -ffunction-sections
    -fdata-sections
)

set_target_properties(${_PD_TARGET} PROPERTIES
    C_STANDARD                99
    C_STANDARD_REQUIRED       ON
    POSITION_INDEPENDENT_CODE ON
)

# Devkitpro's libz is fine for the system headers we expose, but every .c
# bundled here uses its own zlib — so don't link ZLIB::ZLIB (avoids
# duplicate-symbol fights with the bundled zlib above).
