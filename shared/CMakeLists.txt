# Shared static library used by browser + player nros.
#
# foyer_shared = service + data layer (config, library scan, scrapers,
# net, i18n, log, archive). NO rendering deps. Browser + players both
# link this. The browser layers brls on top; the player layers
# Plutonium (SDL2) on top. The legacy nanovg-deko3d foyer_render
# target was removed in 0.7.0's P5 purge.

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
    library/asset_pack.cpp
    library/cheat_installer.cpp
    library/bezel_installer.cpp
    library/per_game_bezel.cpp
    library/foyer_updater.cpp
    library/foyer_update_job.cpp
    library/ra_progress.cpp
    library/scrape_job.cpp
    library/skipped_versions.cpp
    library/switch_titles.cpp
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
    # rcheevos for rc_hash_generate_from_buffer (used by ra_progress
    # to pre-fetch RA progress for the browser). The player binary
    # also pulls this transitively via the libretro frontend, so the
    # shared static-link surface stays small.
    rcheevos
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

