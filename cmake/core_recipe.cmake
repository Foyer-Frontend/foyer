# Helpers used by per-core CMake fragments under cores/<name>.cmake.
#
# A core recipe fetches a libretro core's source tree, compiles its C/C++
# sources into a STATIC library named `core_<name>`, and exposes the include
# directories the libretro frontend needs to call into it.
#
# Each per-core file is responsible for the core-specific source list,
# defines, and any quirks. This file just collects the common boilerplate.

# foyer_core_static_library(
#     NAME           <core_name>
#     SOURCES        <files...>
#     INCLUDE_DIRS   <dirs...>
#     COMPILE_DEFS   <-Dfoo=1...>
#     COMPILE_OPTS   <-Wfoo...>
# )
#
# Produces an INTERFACE-public static lib named `core_<NAME>`.
function(foyer_core_static_library)
    cmake_parse_arguments(C
        ""
        "NAME"
        "SOURCES;INCLUDE_DIRS;COMPILE_DEFS;COMPILE_OPTS"
        ${ARGN})

    if (NOT C_NAME)
        message(FATAL_ERROR "foyer_core_static_library: NAME required")
    endif()
    if (NOT C_SOURCES)
        message(FATAL_ERROR "foyer_core_static_library(${C_NAME}): SOURCES required")
    endif()

    set(_target core_${C_NAME})
    add_library(${_target} STATIC ${C_SOURCES})

    target_include_directories(${_target} PUBLIC ${C_INCLUDE_DIRS})

    if (C_COMPILE_DEFS)
        target_compile_definitions(${_target} PRIVATE ${C_COMPILE_DEFS})
    endif()

    # Cores are "trust me" C99 — silence warnings that aren't actionable.
    target_compile_options(${_target} PRIVATE
        -w
        ${C_COMPILE_OPTS}
    )

    set_target_properties(${_target} PROPERTIES
        C_STANDARD            99
        C_STANDARD_REQUIRED   ON
        POSITION_INDEPENDENT_CODE ON
    )

    set(FOYER_CORE_TARGET ${_target} PARENT_SCOPE)
endfunction()
