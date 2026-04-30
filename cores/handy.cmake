# cores/handy.cmake — libretro-handy (Atari Lynx) core build.
#
# Fetches the upstream source and compiles it as a static library named
# `core_handy`. The player nro links this directly so retro_run() and
# friends are resolved at link time.

include(FetchContent)

FetchContent_Declare(libretro_handy
    GIT_REPOSITORY https://github.com/libretro/libretro-handy.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_handy)

set(_HANDY_DIR  ${libretro_handy_SOURCE_DIR})
set(_HANDY_LR   ${_HANDY_DIR}/libretro)
set(_HANDY_LYNX ${_HANDY_DIR}/lynx)
set(_HANDY_BLIP ${_HANDY_DIR}/blip)
set(_HANDY_COMM ${_HANDY_DIR}/libretro-common)

# Mirror the file list from upstream Makefile.common (non-STATIC_LINKING build
# so we get the libretro-common compat helpers compiled in too).
set(_HANDY_CXX
    ${_HANDY_LYNX}/lynxdec.cpp
    ${_HANDY_LYNX}/cart.cpp
    ${_HANDY_LYNX}/memmap.cpp
    ${_HANDY_LYNX}/mikie.cpp
    ${_HANDY_LYNX}/ram.cpp
    ${_HANDY_LYNX}/rom.cpp
    ${_HANDY_LYNX}/susie.cpp
    ${_HANDY_LYNX}/system.cpp
    ${_HANDY_LYNX}/eeprom.cpp
    ${_HANDY_LR}/libretro.cpp
    ${_HANDY_BLIP}/Blip_Buffer.cpp
    ${_HANDY_BLIP}/Stereo_Buffer.cpp
)

set(_HANDY_C
    ${_HANDY_COMM}/compat/compat_posix_string.c
    ${_HANDY_COMM}/compat/compat_snprintf.c
    ${_HANDY_COMM}/compat/compat_strcasestr.c
    ${_HANDY_COMM}/compat/compat_strl.c
    ${_HANDY_COMM}/compat/fopen_utf8.c
    ${_HANDY_COMM}/encodings/encoding_utf.c
    ${_HANDY_COMM}/file/file_path.c
    ${_HANDY_COMM}/file/file_path_io.c
    ${_HANDY_COMM}/streams/file_stream.c
    ${_HANDY_COMM}/streams/file_stream_transforms.c
    ${_HANDY_COMM}/string/stdstring.c
    ${_HANDY_COMM}/time/rtime.c
    ${_HANDY_COMM}/vfs/vfs_implementation.c
)

# foyer_core_static_library only handles a single language, but this core mixes
# C and C++. Build directly so we can drive both.
add_library(core_handy STATIC ${_HANDY_CXX} ${_HANDY_C})
target_include_directories(core_handy PUBLIC
    ${_HANDY_DIR}
    ${_HANDY_LYNX}
    ${_HANDY_LR}
    ${_HANDY_BLIP}
    ${_HANDY_COMM}/include
)
target_compile_definitions(core_handy PRIVATE
    __LIBRETRO__=1
    SWITCH=1
    __SWITCH__=1
    HAVE_LIBNX=1
    HAVE_STDINT_H=1
    FRONTEND_SUPPORTS_RGB565=1
    WANT_CRC32=1
    "INLINE=inline"
)
target_compile_options(core_handy PRIVATE -w)
set_target_properties(core_handy PROPERTIES
    C_STANDARD                99
    C_STANDARD_REQUIRED       ON
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     ON
    POSITION_INDEPENDENT_CODE ON)
