# cores/genesis_plus_gx.cmake — libretro Genesis Plus GX
# (Mega Drive / Genesis / Master System / Game Gear / SG-1000 / Mega-CD).

include(FetchContent)

FetchContent_Declare(libretro_gpgx
    GIT_REPOSITORY https://github.com/libretro/Genesis-Plus-GX.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_gpgx)

set(_GPGX  ${libretro_gpgx_SOURCE_DIR})
set(_GPGX_LR   ${_GPGX}/libretro)
set(_GPGX_COMM ${_GPGX_LR}/libretro-common)

set(_GPGX_DIRS
    ${_GPGX}/core
    ${_GPGX}/core/z80
    ${_GPGX}/core/m68k
    ${_GPGX}/core/ntsc
    ${_GPGX}/core/sound
    ${_GPGX}/core/sound/minimp3
    ${_GPGX}/core/sound/tremor
    ${_GPGX}/core/input_hw
    ${_GPGX}/core/cd_hw
    ${_GPGX}/core/cart_hw
    ${_GPGX}/core/cart_hw/svp
)
set(_GPGX_C "")
foreach(_d ${_GPGX_DIRS})
    file(GLOB _src "${_d}/*.c")
    list(APPEND _GPGX_C ${_src})
endforeach()

list(APPEND _GPGX_C
    ${_GPGX_LR}/libretro.c
    # libretro-common compat (mirror Makefile.common's STATIC_LINKING != 1 path)
    ${_GPGX_COMM}/streams/file_stream.c
    ${_GPGX_COMM}/streams/file_stream_transforms.c
    ${_GPGX_COMM}/compat/fopen_utf8.c
    ${_GPGX_COMM}/compat/compat_snprintf.c
    ${_GPGX_COMM}/compat/compat_strl.c
    ${_GPGX_COMM}/compat/compat_strcasestr.c
    ${_GPGX_COMM}/compat/compat_posix_string.c
    ${_GPGX_COMM}/encodings/encoding_utf.c
    ${_GPGX_COMM}/file/file_path.c
    ${_GPGX_COMM}/file/retro_dirent.c
    ${_GPGX_COMM}/lists/string_list.c
    ${_GPGX_COMM}/lists/dir_list.c
    ${_GPGX_COMM}/memmap/memalign.c
    ${_GPGX_COMM}/string/stdstring.c
    ${_GPGX_COMM}/vfs/vfs_implementation.c
    # Bundled zlib subset used by the core.
    ${_GPGX_LR}/deps/zlib-1.2.11/adler32.c
    ${_GPGX_LR}/deps/zlib-1.2.11/crc32.c
    ${_GPGX_LR}/deps/zlib-1.2.11/inffast.c
    ${_GPGX_LR}/deps/zlib-1.2.11/inflate.c
    ${_GPGX_LR}/deps/zlib-1.2.11/inftrees.c
    ${_GPGX_LR}/deps/zlib-1.2.11/zutil.c
)

add_library(core_genesisplusgx STATIC ${_GPGX_C})
target_include_directories(core_genesisplusgx PUBLIC
    ${_GPGX_DIRS}
    ${_GPGX_LR}
    ${_GPGX_COMM}/include
)
target_compile_definitions(core_genesisplusgx PRIVATE
    __LIBRETRO__=1
    SWITCH=1
    HAVE_LIBNX=1
    LSB_FIRST=1
    BYTE_ORDER=LITTLE_ENDIAN
    ALIGN_LONG=1
    ALIGN_WORD=1
    M68K_OVERCLOCK_SHIFT=20
    HAVE_ZLIB=1
    "INLINE=static inline"
    USE_LIBTREMOR=1
    USE_PER_SOUND_CHANNELS_CONFIG=1
    USE_16BPP_RENDERING=1
    FRONTEND_SUPPORTS_RGB565=1
)
target_compile_options(core_genesisplusgx PRIVATE -w)
set_target_properties(core_genesisplusgx PROPERTIES
    C_STANDARD                99
    C_STANDARD_REQUIRED       ON
    POSITION_INDEPENDENT_CODE ON)
