# foyer.nro — the browser binary the user installs first.
#
# At Phase 1 it just brings the platform layer up and shows a placeholder
# screen so we can prove the build chain end-to-end. The browser UI grows in
# subsequent phases.

# libhaze gives us a tiny MTP server for the rom drop-off feature. It only
# the browser needs it, so the FetchContent stays scoped here.
include(FetchContent)
FetchContent_Declare(libhaze
    GIT_REPOSITORY https://github.com/ITotalJustice/libhaze.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libhaze)

add_executable(foyer
    src/main.cpp
    src/views.cpp
    src/theme.cpp
    src/launch.cpp
    src/mtp.cpp
    src/seed_assets.cpp
    src/session.cpp
    src/boot_splash.cpp
    src/hos_status.cpp
)

target_link_libraries(foyer PRIVATE foyer_shared libhaze)
target_compile_definitions(foyer PRIVATE FOYER_BROWSER=1)

set_target_properties(foyer PROPERTIES
    C_STANDARD   23
    CXX_STANDARD 26)

# Embed any blobs from assets/embed/ into the binary (placeholder for now).
target_compile_options(foyer PRIVATE
    --embed-dir=${FOYER_EMBED_DIR})

# Bundle assets into a romfs section.
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
dkp_add_asset_target(foyer_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

# nanovg-deko3d builds these shader targets via the FetchContent project; we
# install the compiled .dksh blobs alongside the rest of the romfs.
dkp_install_assets(foyer_romfs
    DESTINATION shaders
    TARGETS
        fill_aa_fsh
        fill_fsh
        fill_vsh
)

# NACP / NRO. These devkitPro helpers come from switch-cmake.
nx_generate_nacp(
    OUTPUT  foyer.nacp
    NAME    foyer
    AUTHOR  foyer
    VERSION ${PROJECT_VERSION})

nx_create_nro(foyer
    OUTPUT ${CMAKE_BINARY_DIR}/foyer.nro
    ICON   ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP   foyer.nacp
    ROMFS  foyer_romfs)

message(STATUS "browser nro -> ${CMAKE_BINARY_DIR}/foyer.nro")
