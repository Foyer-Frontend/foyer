# cores/mupen64plus.cmake — libretro mupen64plus-next (Nintendo 64) core build.
#
# Mirrors the upstream Makefile.libretro `platform=libnx` block:
#   * aarch64 dynarec (NEW_DYNAREC=4)
#   * GLideN64 video plugin against GLES3 + EGL via switch-mesa
#   * RSP-HLE (paraLLEl-RDP / paraLLEl-RSP / angrylion / LLE all OFF)
#   * No NEON paths
#
# Produces the static lib `core_mupen64plus`. The player nro pulls it in,
# along with the EGL / GLESv2 / drm_nouveau libraries from devkitPro
# portlibs (switch-mesa + switch-libdrm_nouveau).

include(FetchContent)

FetchContent_Declare(libretro_mupen64plus
    GIT_REPOSITORY     https://github.com/libretro/mupen64plus-libretro-nx.git
    GIT_TAG            master
    GIT_SHALLOW        TRUE
    GIT_SUBMODULES     ""
    GIT_SUBMODULES_RECURSE FALSE)
FetchContent_MakeAvailable(libretro_mupen64plus)

set(_M64_ROOT       ${libretro_mupen64plus_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Patch upstream's stale switch/mman.h: it calls virtmemReserve() which was
# removed from libnx years ago. Rewrite the file in-tree using the current
# virtmemFindAslr() + virtmemAddReservation() API. new_dynarec.c includes
# this header via a relative path, so we can't shadow it through -I order.
# ---------------------------------------------------------------------------
file(WRITE ${_M64_ROOT}/switch/mman.h
"#ifndef MMAN_H
#define MMAN_H

#ifdef __cplusplus
extern \"C\" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <switch.h>

#define PROT_READ      0b001
#define PROT_WRITE     0b010
#define PROT_EXEC      0b100
#define MAP_PRIVATE    2
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20
#define MAP_FAILED     ((void *)-1)

static void* ptr_rw = NULL;
static VirtmemReservation* ptr_rw_rv = NULL;

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    (void)fd;
    (void)offset;
    size_t size = (len + 0xFFF) & ~0xFFFu;
    virtmemLock();
    ptr_rw = virtmemFindAslr(size, 0);
    if (ptr_rw)
        ptr_rw_rv = virtmemAddReservation(ptr_rw, size);
    virtmemUnlock();
    if (!ptr_rw)
        return MAP_FAILED;
    if (R_SUCCEEDED(svcMapProcessMemory(ptr_rw, envGetOwnProcessHandle(), (u64)addr, size)))
        return ptr_rw;
    virtmemLock();
    if (ptr_rw_rv) virtmemRemoveReservation(ptr_rw_rv);
    virtmemUnlock();
    ptr_rw = NULL;
    ptr_rw_rv = NULL;
    return MAP_FAILED;
}

static inline int mprotect(void *addr, size_t len, int prot)
{
    (void)addr; (void)len; (void)prot;
    return 0;
}

static inline int munmap(void *addr, size_t len)
{
    size_t size = (len + 0xFFF) & ~0xFFFu;
    if (ptr_rw)
        svcUnmapProcessMemory(ptr_rw, envGetOwnProcessHandle(), (u64)addr, size);
    virtmemLock();
    if (ptr_rw_rv) virtmemRemoveReservation(ptr_rw_rv);
    virtmemUnlock();
    ptr_rw = NULL;
    ptr_rw_rv = NULL;
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
")

set(_M64_CORE_DIR   ${_M64_ROOT}/mupen64plus-core)
set(_M64_RSP_HLE    ${_M64_ROOT}/mupen64plus-rsp-hle)
set(_M64_LIBRETRO   ${_M64_ROOT}/libretro)
set(_M64_COMM_DIR   ${_M64_ROOT}/libretro-common)
set(_M64_GLIDEN64   ${_M64_ROOT}/GLideN64)
set(_M64_CUSTOM     ${_M64_ROOT}/custom)
set(_M64_AUDIO      ${_M64_CUSTOM}/mupen64plus-core/plugin/audio_libretro)
set(_M64_MINIZIP    ${_M64_CORE_DIR}/subprojects/minizip)
set(_M64_MD5        ${_M64_CORE_DIR}/subprojects/md5)
set(_M64_LIBPNG     ${_M64_CUSTOM}/dependencies/libpng)
set(_M64_LIBZLIB    ${_M64_CUSTOM}/dependencies/libzlib)
set(_M64_XXHASH     ${_M64_ROOT}/xxHash)

# ---------------------------------------------------------------------------
# Core sources (Makefile.common SOURCES_C, libnx variant)
# ---------------------------------------------------------------------------
set(_M64_CORE_SRC
    ${_M64_CORE_DIR}/src/asm_defines/asm_defines.c
    ${_M64_CORE_DIR}/src/api/callbacks.c
    ${_M64_CUSTOM}/mupen64plus-core/api/config.c
    ${_M64_CORE_DIR}/src/api/debugger.c
    ${_M64_CORE_DIR}/src/api/frontend.c
    ${_M64_CORE_DIR}/src/backends/plugins_compat/audio_plugin_compat.c
    ${_M64_CORE_DIR}/src/backends/api/video_capture_backend.c
    ${_M64_CORE_DIR}/src/backends/plugins_compat/input_plugin_compat.c
    ${_M64_CORE_DIR}/src/backends/clock_ctime_plus_delta.c
    ${_M64_CORE_DIR}/src/backends/dummy_video_capture.c
    ${_M64_CORE_DIR}/src/backends/file_storage.c
    ${_M64_CORE_DIR}/src/device/cart/cart.c
    ${_M64_CORE_DIR}/src/device/cart/af_rtc.c
    ${_M64_CORE_DIR}/src/device/cart/cart_rom.c
    ${_M64_CORE_DIR}/src/device/cart/eeprom.c
    ${_M64_CORE_DIR}/src/device/cart/flashram.c
    ${_M64_CORE_DIR}/src/device/cart/is_viewer.c
    ${_M64_CORE_DIR}/src/device/cart/sram.c
    ${_M64_CORE_DIR}/src/device/controllers/game_controller.c
    ${_M64_CORE_DIR}/src/device/controllers/vru_controller.c
    ${_M64_CORE_DIR}/src/device/controllers/paks/biopak.c
    ${_M64_CORE_DIR}/src/device/controllers/paks/mempak.c
    ${_M64_CORE_DIR}/src/device/controllers/paks/rumblepak.c
    ${_M64_CORE_DIR}/src/device/controllers/paks/transferpak.c
    ${_M64_CORE_DIR}/src/device/dd/dd_controller.c
    ${_M64_CORE_DIR}/src/device/dd/disk.c
    ${_M64_CORE_DIR}/src/device/device.c
    ${_M64_CORE_DIR}/src/device/gb/gb_cart.c
    ${_M64_CORE_DIR}/src/device/gb/mbc3_rtc.c
    ${_M64_CORE_DIR}/src/device/gb/m64282fp.c
    ${_M64_CORE_DIR}/src/device/memory/memory.c
    ${_M64_CORE_DIR}/src/device/pif/bootrom_hle.c
    ${_M64_CORE_DIR}/src/device/pif/cic.c
    ${_M64_CORE_DIR}/src/device/pif/n64_cic_nus_6105.c
    ${_M64_CORE_DIR}/src/device/pif/pif.c
    ${_M64_CORE_DIR}/src/device/r4300/cached_interp.c
    ${_M64_CORE_DIR}/src/device/r4300/cp0.c
    ${_M64_CORE_DIR}/src/device/r4300/cp1.c
    ${_M64_CORE_DIR}/src/device/r4300/cp2.c
    ${_M64_CORE_DIR}/src/device/r4300/idec.c
    ${_M64_CORE_DIR}/src/device/r4300/interrupt.c
    ${_M64_CORE_DIR}/src/device/r4300/pure_interp.c
    ${_M64_CORE_DIR}/src/device/r4300/r4300_core.c
    ${_M64_CORE_DIR}/src/device/r4300/tlb.c
    ${_M64_CORE_DIR}/src/device/rcp/ai/ai_controller.c
    ${_M64_CORE_DIR}/src/device/rcp/mi/mi_controller.c
    ${_M64_CORE_DIR}/src/device/rcp/pi/pi_controller.c
    ${_M64_CORE_DIR}/src/device/rcp/rdp/fb.c
    ${_M64_CORE_DIR}/src/device/rcp/rdp/rdp_core.c
    ${_M64_CORE_DIR}/src/device/rcp/ri/ri_controller.c
    ${_M64_CORE_DIR}/src/device/rcp/rsp/rsp_core.c
    ${_M64_CORE_DIR}/src/device/rcp/si/si_controller.c
    ${_M64_CORE_DIR}/src/device/rcp/vi/vi_controller.c
    ${_M64_CORE_DIR}/src/device/rdram/rdram.c
    ${_M64_CORE_DIR}/src/main/main.c
    ${_M64_CORE_DIR}/src/main/util.c
    ${_M64_CORE_DIR}/src/main/cheat.c
    ${_M64_CORE_DIR}/src/main/rom.c
    ${_M64_CORE_DIR}/src/main/savestates.c
    ${_M64_CORE_DIR}/src/plugin/plugin.c
    ${_M64_CORE_DIR}/src/plugin/dummy_audio.c
    ${_M64_CORE_DIR}/src/plugin/dummy_input.c
    ${_M64_MD5}/md5.c
)

# ---------------------------------------------------------------------------
# aarch64 dynarec (NEW_DYNAREC=4)
# ---------------------------------------------------------------------------
set(_M64_DYNAREC_SRC
    ${_M64_CORE_DIR}/src/device/r4300/new_dynarec/new_dynarec.c
)
set(_M64_DYNAREC_ASM
    ${_M64_CORE_DIR}/src/device/r4300/new_dynarec/arm64/linkage_arm64.S
)
# .S files: assembled by the C compiler with the C preprocessor, which is the
# default for CMake's ASM language when invoked through gcc.
enable_language(ASM)

# ---------------------------------------------------------------------------
# Bundled deps (libpng, minizip — zlib comes from devkitPro portlibs)
# ---------------------------------------------------------------------------
set(_M64_LIBPNG_SRC
    ${_M64_LIBPNG}/png.c
    ${_M64_LIBPNG}/pngerror.c
    ${_M64_LIBPNG}/pngget.c
    ${_M64_LIBPNG}/pngmem.c
    ${_M64_LIBPNG}/pngpread.c
    ${_M64_LIBPNG}/pngread.c
    ${_M64_LIBPNG}/pngrio.c
    ${_M64_LIBPNG}/pngrtran.c
    ${_M64_LIBPNG}/pngrutil.c
    ${_M64_LIBPNG}/pngset.c
    ${_M64_LIBPNG}/pngtrans.c
    ${_M64_LIBPNG}/pngwio.c
    ${_M64_LIBPNG}/pngwrite.c
    ${_M64_LIBPNG}/pngwtran.c
    ${_M64_LIBPNG}/pngwutil.c
)

set(_M64_MINIZIP_SRC
    ${_M64_MINIZIP}/zip.c
    ${_M64_MINIZIP}/unzip.c
    ${_M64_MINIZIP}/ioapi.c
)

# ---------------------------------------------------------------------------
# RSP-HLE
# ---------------------------------------------------------------------------
set(_M64_RSP_HLE_SRC
    ${_M64_RSP_HLE}/src/alist.c
    ${_M64_RSP_HLE}/src/alist_audio.c
    ${_M64_RSP_HLE}/src/alist_naudio.c
    ${_M64_RSP_HLE}/src/alist_nead.c
    ${_M64_RSP_HLE}/src/audio.c
    ${_M64_RSP_HLE}/src/cicx105.c
    ${_M64_RSP_HLE}/src/hle.c
    ${_M64_RSP_HLE}/src/hvqm.c
    ${_M64_RSP_HLE}/src/jpeg.c
    ${_M64_RSP_HLE}/src/memory.c
    ${_M64_RSP_HLE}/src/mp3.c
    ${_M64_RSP_HLE}/src/musyx.c
    ${_M64_RSP_HLE}/src/re2.c
    ${_M64_RSP_HLE}/src/plugin.c
)

# ---------------------------------------------------------------------------
# Libretro frontend + libretro-common
# ---------------------------------------------------------------------------
set(_M64_LIBRETRO_SRC
    ${_M64_LIBRETRO}/libretro.c
    ${_M64_COMM_DIR}/memmap/memalign.c
    ${_M64_CUSTOM}/mupen64plus-core/plugin/emulate_game_controller_via_libretro.c
    ${_M64_COMM_DIR}/audio/resampler/drivers/sinc_resampler.c
    ${_M64_COMM_DIR}/audio/resampler/drivers/nearest_resampler.c
    ${_M64_COMM_DIR}/audio/resampler/audio_resampler.c
    ${_M64_AUDIO}/audio_backend_libretro.c
    ${_M64_COMM_DIR}/file/config_file.c
    ${_M64_COMM_DIR}/file/config_file_userdata.c
    ${_M64_COMM_DIR}/file/file_path.c
    ${_M64_COMM_DIR}/file/file_path_io.c
    ${_M64_COMM_DIR}/time/rtime.c
    ${_M64_COMM_DIR}/compat/compat_strl.c
    ${_M64_COMM_DIR}/compat/compat_posix_string.c
    ${_M64_COMM_DIR}/compat/compat_strcasestr.c
    ${_M64_COMM_DIR}/compat/fopen_utf8.c
    ${_M64_COMM_DIR}/audio/conversion/float_to_s16.c
    ${_M64_COMM_DIR}/audio/conversion/s16_to_float.c
    ${_M64_COMM_DIR}/features/features_cpu.c
    ${_M64_COMM_DIR}/lists/string_list.c
    ${_M64_COMM_DIR}/encodings/encoding_utf.c
    ${_M64_COMM_DIR}/string/stdstring.c
    ${_M64_COMM_DIR}/vfs/vfs_implementation.c
    ${_M64_COMM_DIR}/streams/file_stream.c
    ${_M64_COMM_DIR}/libco/libco.c
    ${_M64_CUSTOM}/mupen64plus-core/api/vidext_libretro.c
    ${_M64_COMM_DIR}/glsm/glsm.c
    ${_M64_COMM_DIR}/glsym/glsym_es3.c
    ${_M64_COMM_DIR}/glsym/rglgen.c
)

# ---------------------------------------------------------------------------
# GLideN64 (video plugin) — order-independent, full GLES3 build
# ---------------------------------------------------------------------------
set(_M64_GLIDEN64_SRC
    ${_M64_GLIDEN64}/src/osal/osal_files_unix.c
    ${_M64_GLIDEN64}/src/Combiner.cpp
    ${_M64_GLIDEN64}/src/CombinerKey.cpp
    ${_M64_GLIDEN64}/src/CommonPluginAPI.cpp
    ${_M64_GLIDEN64}/src/Config.cpp
    ${_M64_GLIDEN64}/src/convert.cpp
    ${_M64_GLIDEN64}/src/DebugDump.cpp
    ${_M64_GLIDEN64}/src/Debugger.cpp
    ${_M64_GLIDEN64}/src/DepthBuffer.cpp
    ${_M64_GLIDEN64}/src/DisplayWindow.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/mupen64plus/mupen64plus_DisplayWindow.cpp
    ${_M64_GLIDEN64}/src/DisplayLoadProgress.cpp
    ${_M64_GLIDEN64}/src/FrameBuffer.cpp
    ${_M64_GLIDEN64}/src/FrameBufferInfo.cpp
    ${_M64_GLIDEN64}/src/GBI.cpp
    ${_M64_GLIDEN64}/src/gDP.cpp
    ${_M64_GLIDEN64}/src/GLideN64.cpp
    ${_M64_GLIDEN64}/src/gSP.cpp
    ${_M64_GLIDEN64}/src/N64.cpp
    ${_M64_GLIDEN64}/src/TextDrawer.cpp
    ${_M64_GLIDEN64}/src/PaletteTexture.cpp
    ${_M64_GLIDEN64}/src/Performance.cpp
    ${_M64_GLIDEN64}/src/PostProcessor.cpp
    ${_M64_GLIDEN64}/src/RDP.cpp
    ${_M64_GLIDEN64}/src/RSP.cpp
    ${_M64_GLIDEN64}/src/SoftwareRender.cpp
    ${_M64_GLIDEN64}/src/TexrectDrawer.cpp
    ${_M64_GLIDEN64}/src/TextureFilterHandler.cpp
    ${_M64_GLIDEN64}/src/Textures.cpp
    ${_M64_GLIDEN64}/src/VI.cpp
    ${_M64_GLIDEN64}/src/ZlutTexture.cpp
    ${_M64_GLIDEN64}/src/common/CommonAPIImpl_common.cpp
    ${_M64_GLIDEN64}/src/DepthBufferRender/ClipPolygon.cpp
    ${_M64_GLIDEN64}/src/DepthBufferRender/DepthBufferRender.cpp
    ${_M64_GLIDEN64}/src/BufferCopy/BlueNoiseTexture.cpp
    ${_M64_GLIDEN64}/src/BufferCopy/ColorBufferToRDRAM.cpp
    ${_M64_GLIDEN64}/src/BufferCopy/DepthBufferToRDRAM.cpp
    ${_M64_GLIDEN64}/src/BufferCopy/RDRAMtoColorBuffer.cpp
    ${_M64_GLIDEN64}/src/GraphicsDrawer.cpp
    ${_M64_GLIDEN64}/src/Graphics/Context.cpp
    ${_M64_GLIDEN64}/src/Graphics/ColorBufferReader.cpp
    ${_M64_GLIDEN64}/src/Graphics/CombinerProgram.cpp
    ${_M64_GLIDEN64}/src/Graphics/ObjectHandle.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLFunctions.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Wrapper.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/ThreadedOpenGl/opengl_WrappedFunctions.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Command.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/ThreadedOpenGl/opengl_ObjectPool.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/ThreadedOpenGl/RingBufferPool.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_Attributes.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_BufferedDrawer.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_CachedFunctions.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_ColorBufferReaderWithBufferStorage.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_ColorBufferReaderWithPixelBuffer.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_ColorBufferReaderWithReadPixels.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_ColorBufferReaderWithEGLImage.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_ContextImpl.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_GLInfo.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_Parameters.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/opengl_Utils.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilder.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactory.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryAccurate.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryFast.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryCommon.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderCommon.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderAccurate.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderFast.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_FXAA.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_ShaderStorage.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_SpecialShadersFactory.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GLSL/glsl_Utils.cpp
    ${_M64_GLIDEN64}/src/Graphics/OpenGLContext/GraphicBuffer/PrivateApi/GraphicBuffer.cpp
    ${_M64_GLIDEN64}/src/mupenplus/MemoryStatus_mupenplus.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3D.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DAM.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DBETA.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DDKR.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX2.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX3.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX095.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX2ACCLAIM.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DEX2CBFD.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DZEX2.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DFLX2.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DGOLDEN.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DPD.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DSETA.cpp
    ${_M64_GLIDEN64}/src/uCodes/F5Indi_Naboo.cpp
    ${_M64_GLIDEN64}/src/uCodes/F5Rogue.cpp
    ${_M64_GLIDEN64}/src/uCodes/F3DTEXA.cpp
    ${_M64_GLIDEN64}/src/uCodes/L3D.cpp
    ${_M64_GLIDEN64}/src/uCodes/L3DEX2.cpp
    ${_M64_GLIDEN64}/src/uCodes/L3DEX.cpp
    ${_M64_GLIDEN64}/src/uCodes/S2DEX2.cpp
    ${_M64_GLIDEN64}/src/uCodes/S2DEX.cpp
    ${_M64_GLIDEN64}/src/uCodes/T3DUX.cpp
    ${_M64_GLIDEN64}/src/uCodes/Turbo3D.cpp
    ${_M64_GLIDEN64}/src/uCodes/ZSort.cpp
    ${_M64_GLIDEN64}/src/uCodes/ZSortBOSS.cpp
    ${_M64_GLIDEN64}/src/MupenPlusPluginAPI.cpp
    ${_M64_GLIDEN64}/src/mupenplus/MupenPlusAPIImpl.cpp
    ${_M64_CUSTOM}/GLideN64/mupenplus/Config_mupenplus.cpp
    ${_M64_CUSTOM}/GLideN64/mupenplus/CommonAPIImpl_mupenplus.cpp
    ${_M64_GLIDEN64}/src/Log.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TextureFilters.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TextureFilters_2xsai.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TextureFilters_hq2x.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TextureFilters_hq4x.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TextureFilters_xbrz.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxCache.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxDbg.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxFilter.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxFilterExport.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxHiResCache.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxHiResNoCache.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxHiResLoader.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxImage.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxQuantize.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxReSample.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxTexCache.cpp
    ${_M64_GLIDEN64}/src/GLideNHQ/TxUtil.cpp
    ${_M64_GLIDEN64}/src/RSP_LoadMatrix.cpp
    ${_M64_GLIDEN64}/src/CRC32_ARMV8.cpp
    ${_M64_GLIDEN64}/src/3DMath.cpp
)

# ---------------------------------------------------------------------------
# Common compile defines (mirrors libnx COREFLAGS + GLES3 GLFLAGS + DYNAFLAGS)
# ---------------------------------------------------------------------------
set(_M64_COMMON_DEFS
    __LIBRETRO__
    __SWITCH__=1
    SWITCH=1
    HAVE_LIBNX
    OS_LINUX
    EGL
    HAVE_OPENGLES
    HAVE_OPENGLES3
    GLES3
    USE_FILE32API
    M64P_PLUGIN_API
    M64P_CORE_PROTOTYPES
    _ENDUSER_RELEASE
    SINC_LOWER_QUALITY
    TXFILTER_LIB
    __VEC4_OPT
    MUPENPLUSAPI
    __STDC_CONSTANT_MACROS
    __STDC_LIMIT_MACROS
    _GLIBCXX_USE_C99_MATH_TR1
    _LDBL_EQ_DBL
    NEW_DYNAREC=4
    DYNAREC
)

set(_M64_INCLUDE_DIRS
    ${_M64_CUSTOM}
    ${_M64_CUSTOM}/mupen64plus-core
    ${_M64_CUSTOM}/android/include
    ${_M64_CUSTOM}/GLideN64
    ${_M64_GLIDEN64}/src
    ${_M64_GLIDEN64}/src/osal
    ${_M64_GLIDEN64}/src/inc
    ${_M64_CORE_DIR}/src
    ${_M64_CORE_DIR}/src/api
    ${_M64_CORE_DIR}/src/asm_defines
    ${_M64_CORE_DIR}/subprojects/md5
    ${_M64_AUDIO}
    ${_M64_COMM_DIR}/include
    ${_M64_LIBRETRO}
    ${_M64_LIBPNG}
    ${_M64_MINIZIP}
    ${_M64_XXHASH}
    ${_M64_ROOT}/switch
    $ENV{DEVKITPRO}/libnx/include
)

# ---------------------------------------------------------------------------
# Build the static library — uses the project's standard core helper, then
# adds the C++ sources separately because foyer_core_static_library() is
# C-only.
# ---------------------------------------------------------------------------
foyer_core_static_library(
    NAME mupen64plus
    SOURCES
        ${_M64_CORE_SRC}
        ${_M64_DYNAREC_SRC}
        ${_M64_DYNAREC_ASM}
        ${_M64_RSP_HLE_SRC}
        ${_M64_LIBRETRO_SRC}
        ${_M64_LIBPNG_SRC}
        ${_M64_MINIZIP_SRC}
        ${_M64_GLIDEN64_SRC}
    INCLUDE_DIRS
        ${_M64_INCLUDE_DIRS}
    COMPILE_DEFS
        ${_M64_COMMON_DEFS}
)

set_target_properties(core_mupen64plus PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD            99)

# Real EGL / GLES from switch-mesa + libdrm_nouveau backing it. zlib comes
# from devkitPro portlibs (libretro frontend already links it via foyer_shared).
target_link_libraries(core_mupen64plus PUBLIC
    EGL
    GLESv2
    glapi
    drm_nouveau
    ZLIB::ZLIB)

# Switch portlibs include path is already implicit on the toolchain, but be
# explicit so #include <EGL/egl.h> et al. resolve from devkitPro portlibs
# instead of any vendored copy.
target_include_directories(core_mupen64plus SYSTEM PUBLIC
    $ENV{DEVKITPRO}/portlibs/switch/include)
