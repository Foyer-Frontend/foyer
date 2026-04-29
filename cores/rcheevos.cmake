# Fetches RetroAchievements/rcheevos and builds it as a static library.
# Used by the libretro player to track + report achievements.

include(FetchContent)

FetchContent_Declare(rcheevos
    GIT_REPOSITORY https://github.com/RetroAchievements/rcheevos.git
    GIT_TAG        develop
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(rcheevos)

set(_RC_DIR ${rcheevos_SOURCE_DIR})

# fceumm and rcheevos both ship internal MD5 implementations with conflicting
# symbol names (md5_init/process/finish/append). Patch rcheevos to prefix its
# functions with rc_md5_ so the linker doesn't see duplicates. Idempotent —
# repeated runs are no-ops once symbols are already prefixed.
file(GLOB_RECURSE _RC_MD5_TARGETS
    ${_RC_DIR}/src/*.c
    ${_RC_DIR}/src/*.h
    ${_RC_DIR}/include/*.h)
foreach(_p ${_RC_MD5_TARGETS})
    file(READ ${_p} _txt)
    string(REGEX REPLACE "([^A-Za-z0-9_])md5_init([^A-Za-z0-9_])"
        "\\1rc_md5_init\\2" _txt "${_txt}")
    string(REGEX REPLACE "([^A-Za-z0-9_])md5_process([^A-Za-z0-9_])"
        "\\1rc_md5_process\\2" _txt "${_txt}")
    string(REGEX REPLACE "([^A-Za-z0-9_])md5_finish([^A-Za-z0-9_])"
        "\\1rc_md5_finish\\2" _txt "${_txt}")
    string(REGEX REPLACE "([^A-Za-z0-9_])md5_append([^A-Za-z0-9_])"
        "\\1rc_md5_append\\2" _txt "${_txt}")
    file(WRITE ${_p} "${_txt}")
endforeach()

# Source list mirrors upstream's Makefile + their stand-alone build.
file(GLOB _RC_SRC
    ${_RC_DIR}/src/*.c
    ${_RC_DIR}/src/rapi/*.c
    ${_RC_DIR}/src/rcheevos/*.c
    ${_RC_DIR}/src/rhash/*.c)

add_library(rcheevos STATIC ${_RC_SRC})

target_include_directories(rcheevos PUBLIC
    ${_RC_DIR}/include
    ${_RC_DIR}/src
    ${CMAKE_SOURCE_DIR}/shared/libretro)   # for libretro.h vendored in foyer

target_compile_options(rcheevos PRIVATE -w)
target_compile_definitions(rcheevos PUBLIC
    RC_CLIENT_SUPPORTS_HASH=1)  # enable rc_client_begin_identify_and_load_game
target_compile_definitions(rcheevos PRIVATE
    RC_DISABLE_LUA=1            # we don't use Lua-based test cases
    RC_NO_STD_THREAD=1)         # Switch threading goes through libnx

set_target_properties(rcheevos PROPERTIES
    C_STANDARD            99
    C_STANDARD_REQUIRED   ON
    POSITION_INDEPENDENT_CODE ON)
