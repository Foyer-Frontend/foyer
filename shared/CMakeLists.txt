# Shared static library used by both browser and player nros.
#
# Provides:
#   - platform/  graphics + audio + input bring-up (deko3d, nanovg, audren, hid)
#   - ui/        widgets + theme primitives built on nanovg
#   - util/      filesystem, path, time helpers
#
# Higher-level concerns (archive, config, library, net, scrapers, retroachievements)
# will plug in here as individual subdirs grow.

add_library(foyer_shared STATIC
    platform/app.cpp
    platform/log.cpp
    library/system_db.cpp
    library/scanner.cpp
    library/config.cpp
    library/per_game.cpp
    library/game_meta.cpp
    library/worker.cpp
    library/core_installer.cpp
    library/core_install_job.cpp
    library/shader_installer.cpp
    library/cheat_installer.cpp
    library/foyer_updater.cpp
    library/foyer_update_job.cpp
    library/scrape_job.cpp
    net/http.cpp
    scrapers/cache.cpp
    scrapers/accounts.cpp
    scrapers/libretro_thumbnails.cpp
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
    nanovg
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
