# cores/flycast.cmake — libretro-flycast (Sega Dreamcast) core build.
#
# Mirrors upstream's `make platform=libnx` recipe:
#   WITH_DYNAREC=arm64, HAVE_GL=1, GLES=1, NO_NVMEM=1, TARGET_NO_AREC=1.
#
# devkitPro now ships switch-mesa (libEGL.a / libGLESv2.a / libglapi.a) and
# switch-libdrm_nouveau, so we link real GLES3 instead of stubbing the headers.
# The vixl-backed SH4 → arm64 dynarec is wired up via core/rec-ARM64/.
#
# A handful of sources need light source-level fixes:
#   - core/libretro/vmem_utils.cpp        : virtmemReserve was removed from
#                                           libnx; rewritten to virtmemFindAslr
#                                           with the recommended reservation
#                                           wrapper.
#   - core/network/naomi_network.cpp      : pulls in `rend/gui.h` (which the
#                                           libretro target doesn't ship); the
#                                           gui_display_notification calls are
#                                           rewritten to no-ops.

include(FetchContent)

FetchContent_Declare(libretro_flycast
    GIT_REPOSITORY https://github.com/libretro/flycast.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_flycast)

set(_FC_DIR        ${libretro_flycast_SOURCE_DIR})
set(_FC_CORE       ${_FC_DIR}/core)
set(_FC_DEPS       ${_FC_CORE}/deps)
set(_FC_COMM       ${_FC_CORE}/libretro-common)
set(_FC_LIBRETRO   ${_FC_CORE}/libretro)

# ---------------------------------------------------------------------------
# Source-level patches (idempotent — re-running cmake on an already patched
# tree is a no-op because the target string isn't present any more).
# ---------------------------------------------------------------------------

# 1) libnx renamed virtmemReserve(size) → virtmemFindAslr(size, guard) +
#    virtmemAddReservation(...). vmem_utils.cpp's three call sites work fine
#    with FindAslr alone (we don't need the formal reservation object on the
#    homebrew-loader path — the addresses we hand back are immediately mapped
#    via svcMapProcessCodeMemory / svcMapProcessMemory).
set(_FC_VMEM_CPP ${_FC_LIBRETRO}/vmem_utils.cpp)
file(READ ${_FC_VMEM_CPP} _txt)
string(REPLACE "virtmemReserve(" "virtmemFindAslr_compat(" _txt "${_txt}")
# Inject a tiny shim once after the libnx <switch.h> include block.
if (NOT _txt MATCHES "virtmemFindAslr_compat\\(size_t")
    string(REPLACE
        "#if defined(HAVE_LIBNX)\n#include <switch.h>\nFILE *\tfmemopen (void *__restrict, size_t, const char *__restrict);\n#endif"
        "#if defined(HAVE_LIBNX)\n#include <switch.h>\nFILE *\tfmemopen (void *__restrict, size_t, const char *__restrict);\nstatic inline void* virtmemFindAslr_compat(size_t size) {\n    virtmemLock();\n    void* p = virtmemFindAslr(size, 0);\n    virtmemUnlock();\n    return p;\n}\n#endif"
        _txt "${_txt}")
endif()
file(WRITE ${_FC_VMEM_CPP} "${_txt}")

# 2) naomi_network.cpp includes rend/gui.h (which the libretro build doesn't
#    ship — gui.h is tied to flycast's standalone UI). Strip the include and
#    no-op the gui_display_notification call sites; the libretro frontend
#    surfaces these via its own messaging API which we don't wire here.
set(_FC_NAOMI_NET_CPP ${_FC_CORE}/network/naomi_network.cpp)
file(READ ${_FC_NAOMI_NET_CPP} _txt)
string(REPLACE "#include \"rend/gui.h\""
    "// foyer: rend/gui.h not built on libretro target\n#define gui_display_notification(msg, ms) ((void)(msg))"
    _txt "${_txt}")
file(WRITE ${_FC_NAOMI_NET_CPP} "${_txt}")

# ---------------------------------------------------------------------------
# Source list — kept in lock-step with Makefile.common's libnx-effective set.
# ---------------------------------------------------------------------------

# Top-level core sources (matches Makefile.common SOURCES_CXX top block).
set(_FC_CORE_TOP_SRC
    ${_FC_CORE}/cheats.cpp
    ${_FC_CORE}/nullDC.cpp
    ${_FC_CORE}/serialize.cpp
    ${_FC_CORE}/stdclass.cpp
)

# hw/arm7 — TARGET_NO_AREC gates out the codegen blocks but the .cpps stay.
set(_FC_ARM7_SRC
    ${_FC_CORE}/hw/arm7/arm_mem.cpp
    ${_FC_CORE}/hw/arm7/arm64.cpp
    ${_FC_CORE}/hw/arm7/arm7.cpp
    ${_FC_CORE}/hw/arm7/virt_arm.cpp
    ${_FC_CORE}/hw/arm7/vbaARM.cpp
)

set(_FC_AICA_SRC
    ${_FC_CORE}/hw/aica/dsp.cpp
    ${_FC_CORE}/hw/aica/dsp_arm64.cpp
    ${_FC_CORE}/hw/aica/dsp_interp.cpp
    ${_FC_CORE}/hw/aica/dsp_x64.cpp
    ${_FC_CORE}/hw/aica/aica.cpp
    ${_FC_CORE}/hw/aica/aica_if.cpp
    ${_FC_CORE}/hw/aica/aica_mem.cpp
    ${_FC_CORE}/hw/aica/sgc_if.cpp
)

set(_FC_HOLLY_SRC
    ${_FC_CORE}/hw/holly/holly_intc.cpp
    ${_FC_CORE}/hw/holly/sb.cpp
    ${_FC_CORE}/hw/holly/sb_mem.cpp
)

set(_FC_GDROM_SRC
    ${_FC_CORE}/hw/gdrom/gdrom_response.cpp
    ${_FC_CORE}/hw/gdrom/gdromv3.cpp
)

set(_FC_MAPLE_SRC
    ${_FC_CORE}/hw/maple/maple_helper.cpp
    ${_FC_CORE}/hw/maple/maple_devs.cpp
    ${_FC_CORE}/hw/maple/maple_if.cpp
    ${_FC_CORE}/hw/maple/maple_cfg.cpp
)

set(_FC_MEM_SRC
    ${_FC_CORE}/hw/mem/_vmem.cpp
    ${_FC_CORE}/hw/mem/vmem32.cpp
)

set(_FC_PVR_SRC
    ${_FC_CORE}/hw/pvr/drkPvr.cpp
    ${_FC_CORE}/hw/pvr/Renderer_if.cpp
    ${_FC_CORE}/hw/pvr/pvr_mem.cpp
    ${_FC_CORE}/hw/pvr/pvr_regs.cpp
    ${_FC_CORE}/hw/pvr/pvr_sb_regs.cpp
    ${_FC_CORE}/hw/pvr/spg.cpp
    ${_FC_CORE}/hw/pvr/ta.cpp
    ${_FC_CORE}/hw/pvr/ta_ctx.cpp
    ${_FC_CORE}/hw/pvr/ta_vtx.cpp
)

set(_FC_REND_TOP_SRC
    ${_FC_CORE}/rend/CustomTexture.cpp
    ${_FC_CORE}/rend/sorter.cpp
    ${_FC_CORE}/rend/TexCache.cpp
)

set(_FC_SH4_SRC
    ${_FC_CORE}/hw/sh4/sh4_mmr.cpp
    ${_FC_CORE}/hw/sh4/sh4_mem.cpp
    ${_FC_CORE}/hw/sh4/sh4_interrupts.cpp
    ${_FC_CORE}/hw/sh4/sh4_rom.cpp
    ${_FC_CORE}/hw/sh4/sh4_core_regs.cpp
    ${_FC_CORE}/hw/sh4/sh4_sched.cpp
    ${_FC_CORE}/hw/sh4/sh4_opcode_list.cpp
)

set(_FC_SH4_INTERP
    ${_FC_CORE}/hw/sh4/interpr/sh4_interpreter.cpp
    ${_FC_CORE}/hw/sh4/interpr/sh4_fpu.cpp
    ${_FC_CORE}/hw/sh4/interpr/sh4_opcodes.cpp
)

set(_FC_SH4_MODULES
    ${_FC_CORE}/hw/sh4/modules/serial.cpp
    ${_FC_CORE}/hw/sh4/modules/rtc.cpp
    ${_FC_CORE}/hw/sh4/modules/bsc.cpp
    ${_FC_CORE}/hw/sh4/modules/tmu.cpp
    ${_FC_CORE}/hw/sh4/modules/ccn.cpp
    ${_FC_CORE}/hw/sh4/modules/intc.cpp
    ${_FC_CORE}/hw/sh4/modules/ubc.cpp
    ${_FC_CORE}/hw/sh4/modules/cpg.cpp
    ${_FC_CORE}/hw/sh4/modules/dmac.cpp
    ${_FC_CORE}/hw/sh4/modules/mmu.cpp
    ${_FC_CORE}/hw/sh4/modules/fastmmu.cpp
)

# Generic sh4-dynarec scaffolding (decoder/driver/blockmanager/shil/ssa).
set(_FC_SH4_DYNA
    ${_FC_CORE}/hw/sh4/dyna/decoder.cpp
    ${_FC_CORE}/hw/sh4/dyna/driver.cpp
    ${_FC_CORE}/hw/sh4/dyna/blockmanager.cpp
    ${_FC_CORE}/hw/sh4/dyna/shil.cpp
    ${_FC_CORE}/hw/sh4/dyna/ssa.cpp
)

set(_FC_NAOMI_SRC
    ${_FC_CORE}/hw/naomi/naomi.cpp
    ${_FC_CORE}/hw/naomi/naomi_cart.cpp
    ${_FC_CORE}/hw/naomi/decrypt.cpp
    ${_FC_CORE}/hw/naomi/m1cartridge.cpp
    ${_FC_CORE}/hw/naomi/m4cartridge.cpp
    ${_FC_CORE}/hw/naomi/awcartridge.cpp
    ${_FC_CORE}/hw/naomi/gdcartridge.cpp
    ${_FC_CORE}/hw/naomi/naomi_m3comm.cpp
    ${_FC_CORE}/network/naomi_network.cpp
)

# GLES renderer (HAVE_GL=1, GLES=1, HAVE_OPENGLES2/3 — no OIT on libnx).
set(_FC_REND_GLES_SRC
    ${_FC_CORE}/rend/gles/gles.cpp
    ${_FC_CORE}/rend/gles/gldraw.cpp
    ${_FC_CORE}/rend/gles/gltex.cpp
    ${_FC_CORE}/rend/gles/postprocess.cpp
)
# libretro-common GL state-machine + GLES symbol loader.
set(_FC_GLSYM_SRC
    ${_FC_COMM}/glsym/rglgen.c
    ${_FC_COMM}/glsm/glsm.c
    ${_FC_COMM}/glsym/glsym_es2.c
)

set(_FC_IMGREAD_SRC
    ${_FC_CORE}/imgread/ImgReader.cpp
    ${_FC_CORE}/imgread/cdi.cpp
    ${_FC_CORE}/imgread/chd.cpp
    ${_FC_CORE}/imgread/common.cpp
    ${_FC_CORE}/imgread/cue.cpp
    ${_FC_CORE}/imgread/gdi.cpp
)

set(_FC_LOG_SRC ${_FC_CORE}/log/LogManagerLibretro.cpp)

set(_FC_REIOS_SRC
    ${_FC_CORE}/reios/reios_elf.cpp
    ${_FC_CORE}/reios/reios.cpp
    ${_FC_CORE}/reios/gdrom_hle.cpp
    ${_FC_CORE}/reios/descrambl.cpp
)

set(_FC_ARCHIVE_SRC
    ${_FC_CORE}/archive/archive.cpp
    ${_FC_CORE}/archive/7zArchive.cpp
    ${_FC_CORE}/archive/ZipArchive.cpp
)

# Dynarec — SH4 → arm64 via vixl, ngen_arm64.S thunk.
set(_FC_REC_ARM64_SRC
    ${_FC_CORE}/rec-ARM64/rec_arm64.cpp
)
set(_FC_REC_ARM64_ASM
    ${_FC_CORE}/rec-ARM64/ngen_arm64.S
)
set(_FC_VIXL_TOP_SRC
    ${_FC_DEPS}/vixl/code-buffer-vixl.cc
    ${_FC_DEPS}/vixl/compiler-intrinsics-vixl.cc
    ${_FC_DEPS}/vixl/cpu-features.cc
    ${_FC_DEPS}/vixl/utils-vixl.cc
)
set(_FC_VIXL_A64_SRC
    ${_FC_DEPS}/vixl/aarch64/assembler-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/cpu-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/cpu-features-auditor-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/decoder-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/disasm-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/instructions-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/instrument-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/logic-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/macro-assembler-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/operands-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/pointer-auth-aarch64.cc
    ${_FC_DEPS}/vixl/aarch64/simulator-aarch64.cc
)

set(_FC_LIBRETRO_SRC
    ${_FC_LIBRETRO}/libretro.cpp
    ${_FC_LIBRETRO}/audiostream.cpp
    ${_FC_LIBRETRO}/common.cpp
    ${_FC_LIBRETRO}/vmem_utils.cpp
)

# libretro-common compat layer (subset Makefile.common pulls).
set(_FC_COMM_SRC
    ${_FC_COMM}/memmap/memalign.c
    ${_FC_COMM}/file/file_path.c
    ${_FC_COMM}/file/retro_dirent.c
    ${_FC_COMM}/vfs/vfs_implementation.c
    ${_FC_COMM}/encodings/encoding_utf.c
    ${_FC_COMM}/compat/compat_strl.c
    ${_FC_COMM}/compat/fopen_utf8.c
    ${_FC_COMM}/compat/compat_strcasestr.c
    ${_FC_COMM}/string/stdstring.c
    ${_FC_COMM}/rthreads/rthreads.c
)

# 7zip + zlib (in-tree) for archive + chd support.
file(GLOB _FC_LZMA_SRC ${_FC_DEPS}/lzma/C/*.c)
file(GLOB _FC_ZLIB_SRC ${_FC_DEPS}/zlib/*.c)
list(FILTER _FC_LZMA_SRC EXCLUDE REGEX "/(LzFindMt|MtCoder|MtDec|Threads|LzFindOpt)\\.c$")
list(FILTER _FC_ZLIB_SRC EXCLUDE REGEX "/(gz.*|compress|example|minigzip)\\.c$")

set(_FC_LIBCHDR_SRC
    ${_FC_DEPS}/libchdr/src/libchdr_bitstream.c
    ${_FC_DEPS}/libchdr/src/libchdr_cdrom.c
    ${_FC_DEPS}/libchdr/src/libchdr_chd.c
    ${_FC_DEPS}/libchdr/src/libchdr_flac.c
    ${_FC_DEPS}/libchdr/src/libchdr_huffman.c
)

file(GLOB _FC_LIBZIP_SRC ${_FC_DEPS}/libzip/*.c)
list(FILTER _FC_LIBZIP_SRC EXCLUDE REGEX "/(mkstemp)\\.c$")

set(_FC_MISC_SRC
    ${_FC_DEPS}/coreio/coreio.cpp
    ${_FC_DEPS}/crypto/sha1.cpp
    ${_FC_DEPS}/crypto/sha256.cpp
    ${_FC_DEPS}/crypto/md5.cpp
    ${_FC_DEPS}/libelf/elf.cpp
    ${_FC_DEPS}/libelf/elf32.cpp
    ${_FC_DEPS}/libelf/elf64.cpp
    ${_FC_DEPS}/chdpsr/cdipsr.cpp
)

set(_FC_XXHASH_SRC ${_FC_DEPS}/xxhash/xxhash.c)
set(_FC_SWITCH_SRC ${_FC_CORE}/deps/switch/stubs.c)

set(_FC_ALL_SRC
    ${_FC_CORE_TOP_SRC}
    ${_FC_ARM7_SRC} ${_FC_AICA_SRC} ${_FC_HOLLY_SRC} ${_FC_GDROM_SRC}
    ${_FC_MAPLE_SRC} ${_FC_MEM_SRC} ${_FC_PVR_SRC} ${_FC_REND_TOP_SRC}
    ${_FC_SH4_SRC} ${_FC_SH4_INTERP} ${_FC_SH4_MODULES} ${_FC_SH4_DYNA}
    ${_FC_NAOMI_SRC}
    ${_FC_REND_GLES_SRC}
    ${_FC_GLSYM_SRC}
    ${_FC_IMGREAD_SRC} ${_FC_LOG_SRC} ${_FC_REIOS_SRC} ${_FC_ARCHIVE_SRC}
    ${_FC_LIBRETRO_SRC}
    ${_FC_REC_ARM64_SRC} ${_FC_REC_ARM64_ASM}
    ${_FC_VIXL_TOP_SRC} ${_FC_VIXL_A64_SRC}
    ${_FC_COMM_SRC}
    ${_FC_LZMA_SRC} ${_FC_ZLIB_SRC} ${_FC_LIBCHDR_SRC} ${_FC_LIBZIP_SRC}
    ${_FC_MISC_SRC} ${_FC_XXHASH_SRC}
    ${_FC_SWITCH_SRC}
)

# ---------------------------------------------------------------------------
# Static library — manually built (foyer_core_static_library is C-only).
# ---------------------------------------------------------------------------
set(_FC_TARGET core_flycast)

enable_language(ASM)
set_source_files_properties(${_FC_REC_ARM64_ASM} PROPERTIES LANGUAGE ASM)

add_library(${_FC_TARGET} STATIC ${_FC_ALL_SRC})

target_include_directories(${_FC_TARGET} PRIVATE
    ${_FC_LIBRETRO}
    ${_FC_CORE}
    ${_FC_DEPS}
    ${_FC_DEPS}/libchdr/include
    ${_FC_DEPS}/lzma/C
    ${_FC_DEPS}/zlib
    ${_FC_DEPS}/flac/include
    ${_FC_COMM}/include
    ${_FC_DEPS}/stb
    ${_FC_DEPS}/vixl
    ${_FC_DEPS}/miniupnpc
    ${_FC_CORE}/network
)
# Switch shim headers (sys/mman.h, ucontext.h) — supplied by upstream.
target_include_directories(${_FC_TARGET} SYSTEM PRIVATE
    ${_FC_CORE}/deps/switch
    /opt/devkitpro/portlibs/switch/include
)

target_compile_definitions(${_FC_TARGET} PRIVATE
    # foyer / Switch baseline
    __LIBRETRO__=1
    SWITCH=1
    __SWITCH__=1
    HAVE_LIBNX=1
    HAVE_STDINT_H=1
    HAVE_STDLIB_H=1
    HAVE_SYS_PARAM_H=1

    # flycast platform identity (from Makefile platform=libnx block)
    TARGET_LIBNX=1
    TARGET_NO_OPENMP=1
    TARGET_NO_NVMEM=1
    TARGET_NO_AREC=1
    # ucontext.h on Switch newlib doesn't ship, and the local shim pulls in
    # a colliding siginfo_t. Skipping the signal/exception path keeps things
    # tidy; the SH4 dynarec doesn't need it for the no-rwx code path.
    TARGET_NO_EXCEPTIONS=1
    FEAT_NO_RWX_PAGES=1
    HAVE_GLSYM_PRIVATE=1

    # arm64 host CPU identifier (matches Makefile HOST_CPU_ARM64).
    HOST_CPU=0x20000006

    # Renderer: real GLES3 via switch-mesa. Mirrors libnx Makefile (HAVE_GL=1,
    # GLES=1) which only emits HAVE_OPENGLES + HAVE_OPENGLES2 — we add
    # HAVE_OPENGLES3 so rglgen_headers.h pulls in <GLES3/gl3.h>.
    # NB: don't define HAVE_OPENGL; glsm.c gates desktop-only calls on it.
    HAVE_GL=1
    HAVE_OPENGLES=1
    HAVE_OPENGLES2=1
    HAVE_OPENGLES3=1
    # NB: no HAVE_OIT — libnx Makefile leaves it off; HAVE_GL3 also off.

    CORE=1

    # Archive / chd
    HAVE_CHD=1
    _7ZIP_ST=1
    USE_FLAC=1
    USE_LZMA=1

    # picotcp options swallowed only by the network sources we don't build,
    # but keep the canonical libnx defaults to avoid surprise.
    PICO_SUPPORT_UDP=1

    NDEBUG=1
)

target_compile_options(${_FC_TARGET} PRIVATE
    -w
    -fcommon
    -fno-strict-aliasing
    -ffast-math
    -funroll-loops
    -ftree-vectorize
    -frename-registers
    -fomit-frame-pointer
    -ffunction-sections
    -fdata-sections
    -ftls-model=local-exec
)

set_target_properties(${_FC_TARGET} PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        ON
    C_STANDARD            99
    C_STANDARD_REQUIRED   ON
    POSITION_INDEPENDENT_CODE ON
)

# C++ specific flags (needed because flycast's C++ relies on these gcc-isms).
target_compile_options(${_FC_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-operator-names>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
)

# Link real GLES3 + EGL via switch-mesa, plus drm_nouveau for the kernel-level
# command submission backend mesa relies on. PUBLIC so the player nro pulls
# them in transitively.
target_link_directories(${_FC_TARGET} PUBLIC
    /opt/devkitpro/portlibs/switch/lib
)
target_link_libraries(${_FC_TARGET} PUBLIC
    EGL
    GLESv2
    glapi
    drm_nouveau
)
