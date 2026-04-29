# cores/gambatte.cmake — libretro-gambatte (Game Boy / GB Color) core build.

include(FetchContent)

FetchContent_Declare(libretro_gambatte
    GIT_REPOSITORY https://github.com/libretro/gambatte-libretro.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_gambatte)

set(_GBC_DIR  ${libretro_gambatte_SOURCE_DIR}/libgambatte/src)
set(_GBC_LR   ${libretro_gambatte_SOURCE_DIR}/libgambatte/libretro)
set(_GBC_COMM ${libretro_gambatte_SOURCE_DIR}/libgambatte/libretro-common)
set(_GBC_COMMON_HDR ${libretro_gambatte_SOURCE_DIR}/common)

set(_GBC_CXX
    ${_GBC_DIR}/bootloader.cpp
    ${_GBC_DIR}/cpu.cpp
    ${_GBC_DIR}/gambatte.cpp
    ${_GBC_DIR}/initstate.cpp
    ${_GBC_DIR}/interrupter.cpp
    ${_GBC_DIR}/interruptrequester.cpp
    ${_GBC_DIR}/gambatte-memory.cpp
    ${_GBC_DIR}/sound.cpp
    ${_GBC_DIR}/statesaver.cpp
    ${_GBC_DIR}/tima.cpp
    ${_GBC_DIR}/video.cpp
    ${_GBC_DIR}/video_libretro.cpp
    ${_GBC_DIR}/mem/cartridge.cpp
    ${_GBC_DIR}/mem/cartridge_libretro.cpp
    ${_GBC_DIR}/mem/huc3.cpp
    ${_GBC_DIR}/mem/memptrs.cpp
    ${_GBC_DIR}/mem/rtc.cpp
    ${_GBC_DIR}/sound/channel1.cpp
    ${_GBC_DIR}/sound/channel2.cpp
    ${_GBC_DIR}/sound/channel3.cpp
    ${_GBC_DIR}/sound/channel4.cpp
    ${_GBC_DIR}/sound/duty_unit.cpp
    ${_GBC_DIR}/sound/envelope_unit.cpp
    ${_GBC_DIR}/sound/length_counter.cpp
    ${_GBC_DIR}/video/ly_counter.cpp
    ${_GBC_DIR}/video/lyc_irq.cpp
    ${_GBC_DIR}/video/next_m0_time.cpp
    ${_GBC_DIR}/video/ppu.cpp
    ${_GBC_DIR}/video/sprite_mapper.cpp
    ${_GBC_LR}/libretro.cpp
)
set(_GBC_C
    ${_GBC_LR}/gambatte_log.c
    ${_GBC_LR}/blipper.c
    ${_GBC_COMM}/compat/compat_posix_string.c
    ${_GBC_COMM}/compat/compat_snprintf.c
    ${_GBC_COMM}/compat/compat_strcasestr.c
    ${_GBC_COMM}/compat/compat_strl.c
    ${_GBC_COMM}/compat/fopen_utf8.c
    ${_GBC_COMM}/encodings/encoding_utf.c
    ${_GBC_COMM}/file/file_path.c
    ${_GBC_COMM}/file/file_path_io.c
    ${_GBC_COMM}/streams/file_stream.c
    ${_GBC_COMM}/streams/file_stream_transforms.c
    ${_GBC_COMM}/string/stdstring.c
    ${_GBC_COMM}/time/rtime.c
    ${_GBC_COMM}/vfs/vfs_implementation.c
)

# foyer_core_static_library only handles a single language, but this core mixes
# C and C++. Build directly so we can drive both.
add_library(core_gambatte STATIC ${_GBC_CXX} ${_GBC_C})
target_include_directories(core_gambatte PUBLIC
    ${_GBC_DIR}
    ${libretro_gambatte_SOURCE_DIR}/libgambatte/include
    ${_GBC_COMMON_HDR}
    ${_GBC_COMMON_HDR}/resample
    ${_GBC_LR}
    ${_GBC_COMM}/include
)
target_compile_definitions(core_gambatte PRIVATE
    __LIBRETRO__=1
    HAVE_STDINT_H=1
    FRONTEND_SUPPORTS_RGB565=1
    PATH_MAX=1024
    VIDEO_RGB565=1
)
target_compile_options(core_gambatte PRIVATE -w)
set_target_properties(core_gambatte PROPERTIES
    C_STANDARD                99
    C_STANDARD_REQUIRED       ON
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     ON
    POSITION_INDEPENDENT_CODE ON)
