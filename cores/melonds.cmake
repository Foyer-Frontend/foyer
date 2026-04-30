# cores/melonds.cmake — libretro-melonds (Nintendo DS) core build.
#
# Heavy C++ DS emulator. Mirrors the upstream `platform=libnx` target from
# Makefile + Makefile.common. JIT is *disabled* on libnx upstream (the
# JIT_ARCH=aarch64 line is commented out in the libnx case because of unfixed
# memory issues), so we follow suit and skip ARMJIT_*/dolphin sources.
#
# Produces a static library named `core_melonds`.

include(FetchContent)

FetchContent_Declare(libretro_melonds
    GIT_REPOSITORY https://github.com/libretro/melonds.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
# melonds ships its own CMakeLists.txt (desktop Qt build); we don't want that
# entered as a subdirectory. Populate the source tree only and drive the build
# ourselves.
FetchContent_GetProperties(libretro_melonds)
if (NOT libretro_melonds_POPULATED)
    FetchContent_Populate(libretro_melonds)
endif()

set(_MELON_ROOT    ${libretro_melonds_SOURCE_DIR})
set(_MELON_DIR     ${_MELON_ROOT}/src)
set(_MELON_RETRO   ${_MELON_DIR}/libretro)
set(_MELON_COMM    ${_MELON_RETRO}/libretro-common)

# ---------------------------------------------------------------------------
# C sources (vendored deps + libretro-common compat helpers)
# ---------------------------------------------------------------------------
set(_MELON_C_SRC
    ${_MELON_DIR}/xxhash/xxhash.c
    ${_MELON_DIR}/tiny-AES-c/aes.c
    ${_MELON_DIR}/fatfs/diskio.c
    ${_MELON_DIR}/fatfs/ff.c
    ${_MELON_DIR}/fatfs/ffsystem.c
    ${_MELON_DIR}/fatfs/ffunicode.c
    ${_MELON_DIR}/sha1/sha1.c
    # libretro-common (matches upstream non-STATIC_LINKING set)
    ${_MELON_COMM}/compat/compat_strl.c
    ${_MELON_COMM}/compat/fopen_utf8.c
    ${_MELON_COMM}/compat/compat_posix_string.c
    ${_MELON_COMM}/compat/compat_strcasestr.c
    ${_MELON_COMM}/encodings/encoding_utf.c
    ${_MELON_COMM}/file/file_path.c
    ${_MELON_COMM}/streams/file_stream.c
    ${_MELON_COMM}/streams/file_stream_transforms.c
    ${_MELON_COMM}/streams/memory_stream.c
    ${_MELON_COMM}/string/stdstring.c
    ${_MELON_COMM}/vfs/vfs_implementation.c
    # threading helpers (HAVE_THREADS)
    ${_MELON_COMM}/rthreads/rthreads.c
    ${_MELON_COMM}/rthreads/rsemaphore.c
)

# ---------------------------------------------------------------------------
# C++ sources — core emulator
# ---------------------------------------------------------------------------
set(_MELON_CXX_CORE
    ${_MELON_DIR}/NDS.cpp
    ${_MELON_DIR}/AREngine.cpp
    ${_MELON_DIR}/ARCodeFile.cpp
    ${_MELON_DIR}/ARM.cpp
    ${_MELON_DIR}/ARMInterpreter.cpp
    ${_MELON_DIR}/ARMInterpreter_ALU.cpp
    ${_MELON_DIR}/ARMInterpreter_Branch.cpp
    ${_MELON_DIR}/ARMInterpreter_LoadStore.cpp
    ${_MELON_DIR}/CP15.cpp
    ${_MELON_DIR}/CRC32.cpp
    ${_MELON_DIR}/DMA.cpp
    ${_MELON_DIR}/DSi.cpp
    ${_MELON_DIR}/DSi_AES.cpp
    ${_MELON_DIR}/DSi_Camera.cpp
    ${_MELON_DIR}/DSi_DSP.cpp
    ${_MELON_DIR}/DSi_I2C.cpp
    ${_MELON_DIR}/DSi_NAND.cpp
    ${_MELON_DIR}/DSi_NDMA.cpp
    ${_MELON_DIR}/DSi_NWifi.cpp
    ${_MELON_DIR}/DSi_SD.cpp
    ${_MELON_DIR}/DSi_SPI_TSC.cpp
    ${_MELON_DIR}/DSiCrypto.cpp
    ${_MELON_DIR}/GBACart.cpp
    ${_MELON_DIR}/GPU.cpp
    ${_MELON_DIR}/GPU2D.cpp
    ${_MELON_DIR}/GPU2D_Soft.cpp
    ${_MELON_DIR}/GPU3D.cpp
    ${_MELON_DIR}/GPU3D_Soft.cpp
    ${_MELON_DIR}/NDSCart.cpp
    ${_MELON_DIR}/NDSCart_SRAMManager.cpp
    ${_MELON_DIR}/RTC.cpp
    ${_MELON_DIR}/Savestate.cpp
    ${_MELON_DIR}/SPI.cpp
    ${_MELON_DIR}/SPU.cpp
    ${_MELON_DIR}/Wifi.cpp
    ${_MELON_DIR}/WifiAP.cpp
    ${_MELON_DIR}/frontend/Util_ROM.cpp
)

# Teakra DSP
set(_MELON_CXX_TEAKRA
    ${_MELON_DIR}/teakra/src/ahbm.cpp
    ${_MELON_DIR}/teakra/src/apbp.cpp
    ${_MELON_DIR}/teakra/src/btdmp.cpp
    ${_MELON_DIR}/teakra/src/disassembler_c.cpp
    ${_MELON_DIR}/teakra/src/disassembler.cpp
    ${_MELON_DIR}/teakra/src/dma.cpp
    ${_MELON_DIR}/teakra/src/memory_interface.cpp
    ${_MELON_DIR}/teakra/src/mmio.cpp
    ${_MELON_DIR}/teakra/src/parser.cpp
    ${_MELON_DIR}/teakra/src/processor.cpp
    ${_MELON_DIR}/teakra/src/teakra_c.cpp
    ${_MELON_DIR}/teakra/src/teakra.cpp
    ${_MELON_DIR}/teakra/src/timer.cpp
)

# libretro frontend glue
set(_MELON_CXX_RETRO
    ${_MELON_RETRO}/config.cpp
    ${_MELON_RETRO}/input.cpp
    ${_MELON_RETRO}/libretro.cpp
    ${_MELON_RETRO}/platform.cpp
    ${_MELON_RETRO}/screenlayout.cpp
    ${_MELON_RETRO}/utils.cpp
)

# ---------------------------------------------------------------------------
# Build the static library via the shared helper, then post-tweak for C++.
# ---------------------------------------------------------------------------
foyer_core_static_library(
    NAME melonds
    SOURCES
        ${_MELON_C_SRC}
        ${_MELON_CXX_CORE}
        ${_MELON_CXX_TEAKRA}
        ${_MELON_CXX_RETRO}
    INCLUDE_DIRS
        ${_MELON_DIR}
        ${_MELON_RETRO}
        ${_MELON_COMM}/include
        ${_MELON_DIR}/teakra/include
        ${_MELON_DIR}/teakra/src
    COMPILE_DEFS
        __LIBRETRO__=1
        SWITCH=1
        __SWITCH__=1
        HAVE_LIBNX=1
        HAVE_STDINT_H=1
        HAVE_THREADS=1
        HAVE_WIFI=1
        MELONDS_VERSION="0.9.3"
)

# foyer_core_static_library() pins C_STANDARD=99 for C cores; melonds is
# predominantly C++17, so override CXX standard on the produced target.
set_target_properties(${FOYER_CORE_TARGET} PROPERTIES
    CXX_STANDARD            17
    CXX_STANDARD_REQUIRED   ON
    CXX_EXTENSIONS          ON)
