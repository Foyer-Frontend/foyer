# foyer.nro — the browser binary.
#
# brls (XITRIX/borealis fork) app shell + per-activity sources. The legacy
# nanovg-direct view code (views.cpp/theme.cpp/boot_splash.cpp/session.cpp/
# switch_titles.cpp/seed_assets.cpp) was removed in v0.6.123 once the brls
# carve-out activities (HomeActivity, SystemActivity, GameActivity, …)
# covered every surface that used to live in views.cpp.

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
    src/action_button.cpp
    src/splash_activity.cpp
    src/theme_watcher.cpp
    src/masked_input_cell.cpp
    src/update_check.cpp
    src/install_queue.cpp
    src/per_game_activity.cpp
    src/per_system_activity.cpp
    src/download_queue_activity.cpp
    src/log_viewer_activity.cpp
    src/mtp.cpp

    # borealis Switch platform glue (matches borealis_template demo).
    ${borealis_SOURCE_DIR}/library/lib/platforms/switch/switch_wrapper.c
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

# Bundle assets into a romfs section. We ship borealis's resources/
# tree (font, theme XML, i18n yaml, material symbols, icons) flattened
# into romfs:/, plus the small foyer-side bits (xml/, i18n/, img/,
# bezels/, splash_bg.png) — but NOT systems/ + themes/. Those used to
# be 46 MB of splash + background art shipped in-binary; foyer.nro
# sat at ~54 MB and tripped the hbloader unmap rejection
# (MAKERESULT(347, 26)) when chain-launching cores. Art now ships as
# a separately-released foyer-assets.zip that the browser downloads
# into /foyer/data/assets/ on first run (see shared/library/asset_pack).
# Purge stale staging from prior configures — without this, switching
# from a build that bundled systems/themes (everything ≤ v0.6.65) to
# one that excludes them leaves the old art in romfs and the nro
# stays at 54 MB.
file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/romfs)
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/romfs
     PATTERN "systems" EXCLUDE
     PATTERN "themes"  EXCLUDE)
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
