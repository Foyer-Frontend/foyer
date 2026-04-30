# cores/flycast.cmake — libretro-flycast (Sega Dreamcast) core build.
#
# Mirrors upstream's `make platform=libnx` recipe. flycast is a large C++ core
# with its own libretro-common copy under core/libretro-common; we feed both
# the engine and the libretro-common compat helpers into a single static lib
# `core_flycast` so the player nro can link them in. Vulkan/glslang and the
# texture-upscaler/OpenMP paths are omitted because Switch ships with neither.

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
# Source list — kept in lock-step with Makefile.common's libnx-effective set.
# ---------------------------------------------------------------------------
file(GLOB _FC_AICA_SRC      ${_FC_CORE}/hw/aica/*.cpp)
file(GLOB _FC_ARM7_SRC      ${_FC_CORE}/hw/arm7/*.cpp)
file(GLOB _FC_HOLLY_SRC     ${_FC_CORE}/hw/holly/*.cpp)
file(GLOB _FC_GDROM_SRC     ${_FC_CORE}/hw/gdrom/*.cpp)
file(GLOB _FC_MAPLE_SRC     ${_FC_CORE}/hw/maple/*.cpp)
file(GLOB _FC_MEM_SRC       ${_FC_CORE}/hw/mem/*.cpp)
file(GLOB _FC_PVR_SRC       ${_FC_CORE}/hw/pvr/*.cpp)
file(GLOB _FC_SH4_SRC       ${_FC_CORE}/hw/sh4/*.cpp)
file(GLOB _FC_SH4_INTERP    ${_FC_CORE}/hw/sh4/interpr/*.cpp)
file(GLOB _FC_SH4_MODULES   ${_FC_CORE}/hw/sh4/modules/*.cpp)
set(_FC_SH4_DYNA "")  # dynarec disabled (TARGET_NO_REC)
file(GLOB _FC_NAOMI_SRC     ${_FC_CORE}/hw/naomi/*.cpp)
file(GLOB _FC_REND_SRC      ${_FC_CORE}/rend/*.cpp)
# GLES backends require GLES3/GLES2 headers that devkitA64 doesn't ship.
# foyer renders the libretro framebuffer through nanovg/dkA3d, so we build
# flycast with NO_REND=1 (norend stub) and let the player do the blit.
set(_FC_REND_GLES_SRC ${_FC_CORE}/rend/norend/norend.cpp)

# A handful of TUs unconditionally `#include "rend/gles/gles.h"`, which in
# turn pulls in <GLES3/gl3.h> via libretro-common's rglgen_headers.h. Drop
# in empty stub headers so the compiler is happy — no GL code is reachable
# because we keep HAVE_OPENGL / HAVE_OPENGLES undefined.
set(_FC_GL_STUB_DIR ${CMAKE_BINARY_DIR}/flycast_gl_stubs)
file(MAKE_DIRECTORY ${_FC_GL_STUB_DIR}/GLES3 ${_FC_GL_STUB_DIR}/GLES2 ${_FC_GL_STUB_DIR}/GL)
file(WRITE ${_FC_GL_STUB_DIR}/GLES3/gl3.h    "#pragma once\n")
file(WRITE ${_FC_GL_STUB_DIR}/GLES3/gl3ext.h "#pragma once\n")
file(WRITE ${_FC_GL_STUB_DIR}/GLES2/gl2.h    "#pragma once\n")
file(WRITE ${_FC_GL_STUB_DIR}/GLES2/gl2ext.h "#pragma once\n")
file(WRITE ${_FC_GL_STUB_DIR}/GL/gl.h        "#pragma once\n")
file(WRITE ${_FC_GL_STUB_DIR}/GL/glext.h     "#pragma once\n")

# libretro.cpp issues a few `glsm_ctl(...)` calls that aren't behind a
# HAVE_OPENGL guard but inside the threaded-rendering path. Comment them
# out at configure time — we don't link glsm because there's no GL on
# the Switch path. Idempotent.
set(_FC_LIBRETRO_CPP ${_FC_LIBRETRO}/libretro.cpp)
file(READ ${_FC_LIBRETRO_CPP} _txt)
string(REPLACE "glsm_ctl(GLSM_CTL_STATE_BIND, NULL);"
    "(void)0; // glsm_ctl(GLSM_CTL_STATE_BIND)" _txt "${_txt}")
string(REPLACE "glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);"
    "(void)0; // glsm_ctl(GLSM_CTL_STATE_UNBIND)" _txt "${_txt}")
file(WRITE ${_FC_LIBRETRO_CPP} "${_txt}")

# serialize.cpp unconditionally includes rend/gles/gles.h to pull in struct
# layouts shared with the renderer; with NO_REND and no GLES headers, that
# include just brings in unwanted typedefs. Stub it out.
set(_FC_SERIALIZE_CPP ${_FC_CORE}/serialize.cpp)
file(READ ${_FC_SERIALIZE_CPP} _txt)
string(REPLACE "#include \"rend/gles/gles.h\""
    "#include \"rend/TexCache.h\"" _txt "${_txt}")
file(WRITE ${_FC_SERIALIZE_CPP} "${_txt}")
set(_FC_IMGREAD_SRC
    ${_FC_CORE}/imgread/ImgReader.cpp
    ${_FC_CORE}/imgread/cdi.cpp
    ${_FC_CORE}/imgread/chd.cpp
    ${_FC_CORE}/imgread/common.cpp
    ${_FC_CORE}/imgread/cue.cpp
    ${_FC_CORE}/imgread/gdi.cpp
)
# LogManager.cpp pulls in a Windows-only ConsoleListener.h; only the
# libretro shim is needed.
set(_FC_LOG_SRC ${_FC_CORE}/log/LogManagerLibretro.cpp)
file(GLOB _FC_REIOS_SRC     ${_FC_CORE}/reios/*.cpp)
file(GLOB _FC_ARCHIVE_SRC   ${_FC_CORE}/archive/*.cpp)

set(_FC_CORE_TOP_SRC
    ${_FC_CORE}/cheats.cpp
    ${_FC_CORE}/nullDC.cpp
    ${_FC_CORE}/serialize.cpp
    ${_FC_CORE}/stdclass.cpp
)

set(_FC_LIBRETRO_SRC
    ${_FC_LIBRETRO}/libretro.cpp
    ${_FC_LIBRETRO}/audiostream.cpp
    ${_FC_LIBRETRO}/common.cpp
    ${_FC_LIBRETRO}/vmem_utils.cpp
)

# Disable dynarec entirely (TARGET_NO_REC) — newer libnx renamed
# virtmemReserve and the SH4 JIT brings vmem_utils + vixl + rec-ARM64
# along with it. The interpreter path is enough to verify linkage.
set(_FC_DYNAREC_DISABLED ON)
if (_FC_DYNAREC_DISABLED)
    list(REMOVE_ITEM _FC_LIBRETRO_SRC ${_FC_LIBRETRO}/vmem_utils.cpp)
endif()

# Network-dependent sources (modem/bba) — left out: ENABLE_MODEM=0 on libnx.
# Vulkan + glslang — left out: HAVE_VULKAN=0 on Switch.
# xbrz texture upscaler — left out: HAVE_TEXUPSCALE relies on -fopenmp.

# Dynarec disabled — see TARGET_NO_REC. No rec-ARM64, no vixl, no asm.
set(_FC_REC_ARM64_SRC "")
set(_FC_VIXL_TOP_SRC "")
set(_FC_VIXL_A64_SRC "")
set(_FC_REC_ARM64_ASM "")

# libretro-common compat layer (only the subset Makefile.common pulls).
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
# Some lzma sources are MSVC/Windows-only; filter them.
list(FILTER _FC_LZMA_SRC EXCLUDE REGEX "/(LzFindMt|MtCoder|MtDec|Threads|LzFindOpt)\\.c$")
# zlib's gzlib/gzread/gzwrite/gzclose pull in stdio fseeko/ftello which
# devkitA64 newlib lacks; not needed by libchdr's inflate path.
list(FILTER _FC_ZLIB_SRC EXCLUDE REGEX "/(gz.*|compress|example|minigzip)\\.c$")

# libchdr (CHD reader, optional but cheap to keep on).
set(_FC_LIBCHDR_SRC
    ${_FC_DEPS}/libchdr/src/libchdr_bitstream.c
    ${_FC_DEPS}/libchdr/src/libchdr_cdrom.c
    ${_FC_DEPS}/libchdr/src/libchdr_chd.c
    ${_FC_DEPS}/libchdr/src/libchdr_flac.c
    ${_FC_DEPS}/libchdr/src/libchdr_huffman.c
)

# libzip — used by archive/ZipArchive.cpp.
file(GLOB _FC_LIBZIP_SRC ${_FC_DEPS}/libzip/*.c)
# These pull in win32-specific or POSIX sysdep headers we don't need.
list(FILTER _FC_LIBZIP_SRC EXCLUDE REGEX "/(mkstemp)\\.c$")

# crypto + io helpers used by reios + chd + 7z dispatch.
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

# xxhash (single-file).
set(_FC_XXHASH_SRC ${_FC_DEPS}/xxhash/xxhash.c)

# Switch port stubs (replaces missing posix bits).
set(_FC_SWITCH_SRC ${_FC_CORE}/deps/switch/stubs.c)

# Synthesise a small TU that provides empty implementations for symbols
# normally supplied by the dynarec / vmem subsystems we disabled. The
# code here will never run; it just lets the static link succeed.
set(_FC_FOYER_STUBS ${CMAKE_BINARY_DIR}/flycast_foyer_stubs.cpp)
file(WRITE ${_FC_FOYER_STUBS} [=[
// Auto-generated by foyer's flycast.cmake.
// Provides no-op definitions for symbols that the disabled dynarec, vmem,
// and renderer subsystems would normally supply. The flycast static lib
// can link cleanly into the player nro this way; the symbols themselves
// are never invoked at runtime because the surrounding subsystems aren't
// initialised.
#include "stdclass.h"
#include "hw/mem/_vmem.h"

extern "C" void DYNACALL do_sqw_nommu_area_3(u32 dst, u8* sqb) {
    (void)dst; (void)sqb;
}

void vmem_platform_create_mappings(const vmem_mapping*, unsigned) {}
void vmem_platform_destroy() {}

bool mem_region_unlock(void*, std::size_t) { return true; }
bool mem_region_lock(void*, std::size_t)   { return true; }
bool mem_region_set_exec(void*, std::size_t) { return true; }
void* mem_region_reserve(void*, std::size_t) { return nullptr; }
bool mem_region_release(void*, std::size_t) { return true; }
void* mem_region_map_file(void*, std::size_t) { return nullptr; }
bool mem_region_unmap_file(void*, std::size_t) { return true; }

void bm_Periodical_1s() {}
void bm_Reset() {}

// Renderer globals normally defined in rend/gles/gles.cpp.
int screen_width  = 640;
int screen_height = 480;
]=])

# Stub naomi_network.cpp because the upstream version pulls in rend/gui.h
# (UI code we don't compile) and miniupnpc/picotcp (modem stack we keep
# off). Only the methods naomi_m3comm.cpp calls need to be present.
set(_FC_NAOMI_NETWORK_STUB ${CMAKE_BINARY_DIR}/flycast_naomi_network_stub.cpp)
file(WRITE ${_FC_NAOMI_NETWORK_STUB} [=[
#include "network/naomi_network.h"
void NaomiNetwork::terminate() {}
void NaomiNetwork::shutdown() {}
bool NaomiNetwork::startNetwork() { return false; }
void NaomiNetwork::pipeSlaves() {}
bool NaomiNetwork::receive(u8*, u32) { return false; }
void NaomiNetwork::send(u8*, u32) {}
bool NaomiNetwork::init() { return false; }
]=])

# Generic JIT recompiler (cpp-emitted) — kept off; libnx Makefile leaves it
# enabled but its arm64 codepath doubles up with rec-ARM64. Disable to keep
# things small and avoid duplicate symbols.
# (DYNAREC is still active because rec-ARM64 sources are present.)

set(_FC_ALL_SRC
    ${_FC_CORE_TOP_SRC}
    ${_FC_AICA_SRC} ${_FC_ARM7_SRC} ${_FC_HOLLY_SRC} ${_FC_GDROM_SRC}
    ${_FC_MAPLE_SRC} ${_FC_MEM_SRC} ${_FC_PVR_SRC}
    ${_FC_SH4_SRC} ${_FC_SH4_INTERP} ${_FC_SH4_MODULES} ${_FC_SH4_DYNA}
    ${_FC_NAOMI_SRC} ${_FC_REND_SRC} ${_FC_REND_GLES_SRC}
    ${_FC_IMGREAD_SRC} ${_FC_LOG_SRC} ${_FC_REIOS_SRC} ${_FC_ARCHIVE_SRC}
    ${_FC_LIBRETRO_SRC}
    ${_FC_REC_ARM64_SRC} ${_FC_REC_ARM64_ASM}
    ${_FC_VIXL_TOP_SRC} ${_FC_VIXL_A64_SRC}
    ${_FC_COMM_SRC}
    ${_FC_LZMA_SRC} ${_FC_ZLIB_SRC} ${_FC_LIBCHDR_SRC} ${_FC_LIBZIP_SRC}
    ${_FC_MISC_SRC} ${_FC_XXHASH_SRC}
    ${_FC_SWITCH_SRC}
    ${_FC_FOYER_STUBS}
    ${_FC_NAOMI_NETWORK_STUB}
)

# ---------------------------------------------------------------------------
# Static library — manually built (foyer_core_static_library is C-only).
# ---------------------------------------------------------------------------
set(_FC_TARGET core_flycast)

# .S sources have to be flagged as ASM to pick up the C preprocessor and
# the right assembler flags from devkitA64.
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
    ${_FC_COMM}/include
    ${_FC_DEPS}/stb
    ${_FC_DEPS}/miniupnpc
    ${_FC_CORE}/network
    ${_FC_GL_STUB_DIR}
)
# Switch shim headers (sys/mman.h, malloc.h hoist) — supplied by upstream.
target_include_directories(${_FC_TARGET} SYSTEM PRIVATE
    ${_FC_CORE}/deps/switch
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
    TARGET_NO_REC=1
    TARGET_NO_NVMEM=1
    TARGET_NO_EXCEPTIONS=1
    FEAT_NO_RWX_PAGES=1
    HAVE_GLSYM_PRIVATE=1

    HOST_CPU=0x20000006

    # Renderer: stub (NO_REND). devkitA64 doesn't ship GLES headers, and
    # foyer's frontend draws the libretro framebuffer through nanovg.
    NO_REND=1

    # Archive / chd
    HAVE_CHD=1
    _7ZIP_ST=1
    USE_FLAC=1
    USE_LZMA=1

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
)

# C++17 keeps things compiling for vixl + flycast's modern C++ touches.
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
