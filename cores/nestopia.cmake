# cores/nestopia.cmake — libretro Nestopia UE (NES, alternative to fceumm).

include(FetchContent)

FetchContent_Declare(libretro_nestopia
    GIT_REPOSITORY https://github.com/libretro/nestopia.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libretro_nestopia)

set(_NES  ${libretro_nestopia_SOURCE_DIR})
set(_NES_LR ${_NES}/libretro)

set(_NES_DIRS
    ${_NES}/source/core
    ${_NES}/source/core/api
    ${_NES}/source/core/board
    ${_NES}/source/core/input
    ${_NES}/source/core/vssystem
)
set(_NES_CXX "")
foreach(_d ${_NES_DIRS})
    file(GLOB _src "${_d}/*.cpp")
    list(APPEND _NES_CXX ${_src})
endforeach()
list(APPEND _NES_CXX ${_NES_LR}/libretro.cpp)

# Upstream NstApiVideo.hpp unconditionally defines NST_NO_HQ2X / NST_NO_2XSAI /
# NST_NO_XBR / NST_NO_SCALEX, which means the matching filter source files
# reference forward-declarations the renderer never produces. Drop them.
list(FILTER _NES_CXX EXCLUDE REGEX "NstVideoFilter(HqX|2xSaI|xBR|ScaleX)\\.cpp$")

add_library(core_nestopia STATIC ${_NES_CXX})
target_include_directories(core_nestopia PUBLIC
    ${_NES_DIRS}
    ${_NES_LR}
    ${_NES_LR}/libretro-common/include
)
target_compile_definitions(core_nestopia PRIVATE
    __LIBRETRO__=1
    SWITCH=1
    HAVE_LIBNX=1
    NST_PRAGMA_ONCE_SUPPORT=1
    NST_NO_ZLIB=1
    HAVE_STDINT_H=1
)
target_compile_options(core_nestopia PRIVATE -w -fno-strict-aliasing)
set_target_properties(core_nestopia PROPERTIES
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     ON
    POSITION_INDEPENDENT_CODE ON)
