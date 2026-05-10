# Shared static libraries used by browser + player nros.
#
# 0.6.0 split:
#   - foyer_shared  = service + data layer (config, library scan, scrapers,
#                      net, i18n, log, archive). NO rendering deps. Browser +
#                      players both link this.
#   - foyer_render  = legacy nanovg-deko3d App shell. Players still use this
#                      for libretro frame presentation; the browser switched
#                      to borealis in 0.6.0 and no longer links it.

add_library(foyer_shared STATIC
    platform/log.cpp
    platform/services.cpp
    i18n/i18n.cpp
    i18n/i18n_es.cpp
    i18n/i18n_pt_br.cpp
    library/system_db.cpp
    library/scanner.cpp
    library/library_cache.cpp
    library/config.cpp
    library/per_game.cpp
    library/game_meta.cpp
    library/worker.cpp
    library/core_installer.cpp
    library/core_install_job.cpp
    library/shader_installer.cpp
    library/cheat_installer.cpp
    library/bezel_installer.cpp
    library/foyer_updater.cpp
    library/foyer_update_job.cpp
    library/scrape_job.cpp
    library/skipped_versions.cpp
    library/updates.cpp
    net/http.cpp
    scrapers/cache.cpp
    scrapers/accounts.cpp
    scrapers/libretro_thumbnails.cpp
    scrapers/libretro_index.cpp
    scrapers/screenscraper.cpp
    scrapers/steamgriddb.cpp
    util/archive.cpp
)

target_include_directories(foyer_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(foyer_shared PUBLIC
    FOYER_VERSION="${PROJECT_VERSION}"
    FOYER_DISPLAY_VERSION="${FOYER_DISPLAY_VERSION}"
    FOYER_GIT_COMMIT="${FOYER_GIT_COMMIT}"
)

target_link_libraries(foyer_shared PUBLIC
    stb_headers
    yyjson
    CURL::libcurl
    archive
    lzma
    bz2
    zstd
    lz4
    ZLIB::ZLIB
)

# Compile flags shared with consumers
target_compile_options(foyer_shared PUBLIC
    -Wall
    -Wextra
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-format-truncation
    -fstrict-aliasing
)

# Render shell: nanovg-deko3d App. Players link this for libretro frame
# presentation; browser uses borealis instead.
add_library(foyer_render STATIC
    platform/app.cpp
)

target_link_libraries(foyer_render PUBLIC
    foyer_shared
    nanovg
)
