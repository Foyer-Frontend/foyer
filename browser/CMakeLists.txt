# foyer.nro — the browser binary.
#
# 0.6.0 Phase A: app shell flips from the legacy nanovg-direct renderer to
# borealis (XITRIX/borealis fork). This commit lands the build wiring + a
# stub HomeActivity that proves brls boots on Switch. Existing service-layer
# code (launch, mtp, seed_assets, session, switch_titles, library scanner,
# updater) survives untouched. Legacy view code (views.cpp, theme.cpp,
# boot_splash.cpp, hos_status.cpp) lives on disk but is NOT compiled — it'll
# be cherry-picked back into the brls views in Phases B–E.

include(FetchContent)

# libhaze gives us a tiny MTP server for the rom drop-off feature. It only
# the browser needs it, so the FetchContent stays scoped here.
FetchContent_Declare(libhaze
    GIT_REPOSITORY https://github.com/ITotalJustice/libhaze.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libhaze)

add_executable(foyer
    src/main.cpp
    src/home_activity.cpp
    src/settings_activity.cpp
    src/settings_tab.cpp
    src/system_activity.cpp
    src/game_activity.cpp
    src/search_activity.cpp
    src/power_activity.cpp
    src/power_actions.cpp
    src/wizard_activity.cpp
    src/first_run.cpp
    src/manifest_cache.cpp
    src/hos_status.cpp
    src/library_state.cpp
    src/launch.cpp
    src/self_update.cpp

    # borealis Switch platform glue (matches borealis_template demo).
    ${borealis_SOURCE_DIR}/library/lib/platforms/switch/switch_wrapper.c

    # Still deferred from the brls build: mtp / seed_assets / session /
    # switch_titles. They rely on the legacy nanovg paths (NACP icon
    # handles, View enum from views.hpp) and need refactoring before
    # they fit in. mtp and seed_assets come back in alpha.E; switch_titles
    # rejoins with hos_status in Phase F.
)

target_include_directories(foyer PRIVATE
    include
)

target_link_libraries(foyer PRIVATE
    foyer_shared
    borealis
    libhaze
    # borealis on Switch deko3d backend pulls glfw3 for input; the rest are
    # the linker libs every libnx graphics binary needs.
    glfw3
    EGL
    glapi
    drm_nouveau
)

target_compile_definitions(foyer PRIVATE
    FOYER_BROWSER=1
    BRLS_RESOURCES="romfs:/")

set_target_properties(foyer PROPERTIES
    C_STANDARD   23
    CXX_STANDARD 20)  # std::span via system_db; borealis itself is C++20

# Embed any blobs from assets/embed/ into the binary (placeholder for now).
target_compile_options(foyer PRIVATE
    --embed-dir=${FOYER_EMBED_DIR})

# Bundle assets into a romfs section. We ship our own /themes, /splash,
# etc. AS WELL AS borealis's resources/ tree (font, theme XML, i18n yaml,
# material symbols, icons) flattened into romfs:/.
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/romfs)
file(COPY ${borealis_SOURCE_DIR}/resources/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/romfs)

# brls's nanovg-deko3d backend reads its rendering shaders from
# romfs:/shaders/{fill_vsh,fill_aa_fsh,fill_fsh}.dksh at runtime. The
# borealis source ships the .glsl sources; we compile them with uam
# (devkitPro's shader compiler) into the romfs at configure time.
# Replicates gen_dksh() from borealis/cmake/toolchain.cmake — we don't
# include that toolchain wholesale, so the macro isn't in scope.
find_program(FOYER_UAM_EXE NAMES uam HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
set(_brls_shader_src ${borealis_SOURCE_DIR}/library/lib/extern/nanovg/deko3d/shaders)
set(_brls_shader_out ${CMAKE_CURRENT_BINARY_DIR}/romfs/shaders)
file(MAKE_DIRECTORY ${_brls_shader_out})
execute_process(COMMAND ${FOYER_UAM_EXE} -s vert -o ${_brls_shader_out}/fill_vsh.dksh    ${_brls_shader_src}/fill_vsh.glsl    TIMEOUT 10)
execute_process(COMMAND ${FOYER_UAM_EXE} -s frag -o ${_brls_shader_out}/fill_aa_fsh.dksh ${_brls_shader_src}/fill_aa_fsh.glsl TIMEOUT 10)
execute_process(COMMAND ${FOYER_UAM_EXE} -s frag -o ${_brls_shader_out}/fill_fsh.dksh    ${_brls_shader_src}/fill_fsh.glsl    TIMEOUT 10)

dkp_add_asset_target(foyer_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

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
