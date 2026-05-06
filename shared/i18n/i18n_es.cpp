// Spanish catalogue.
//
// Initial pass — AI-assisted translation pending native-speaker
// review. Slots left intentionally null fall through to English at
// lookup time, so partial translations are safe to ship.
//
// Add a string:
//   1. Append the StringId to the enum in i18n.hpp.
//   2. Add the English text in kEnglishStrings (i18n.cpp).
//   3. Add the Spanish text here (or leave the slot at nullptr to
//      fall through).

#include "i18n.hpp"

#include <array>
#include <cstddef>

namespace foyer::i18n {

constexpr std::size_t kCount =
    static_cast<std::size_t>(StringId::kStringIdCount);

extern const std::array<const char*, kCount> kSpanishStrings;

const std::array<const char*, kCount> kSpanishStrings = [] {
    std::array<const char*, kCount> a{};
    a[(std::size_t)StringId::Home]                       = "Inicio";
    a[(std::size_t)StringId::Library]                    = "Biblioteca";
    a[(std::size_t)StringId::Settings]                   = "Ajustes";
    a[(std::size_t)StringId::Search]                     = "Buscar";
    a[(std::size_t)StringId::Updates]                    = "Actualizaciones";
    a[(std::size_t)StringId::Back]                       = "Atrás";
    a[(std::size_t)StringId::Quit]                       = "Salir";

    a[(std::size_t)StringId::SettingsDisplay]            = "Pantalla";
    a[(std::size_t)StringId::SettingsEmulator]           = "Emulador";
    a[(std::size_t)StringId::SettingsLibrary]            = "Biblioteca";
    a[(std::size_t)StringId::SettingsScrapers]           = "Scrapers";
    a[(std::size_t)StringId::SettingsNetworking]         = "Red";
    a[(std::size_t)StringId::SettingsAbout]              = "Acerca de";

    a[(std::size_t)StringId::UpdatesCheckForUpdates]     = "Buscar actualizaciones";
    a[(std::size_t)StringId::UpdatesUpToDate]            = "Está actualizado";
    a[(std::size_t)StringId::UpdatesAvailable]           = "Actualización disponible";
    a[(std::size_t)StringId::UpdatesInstall]             = "Instalar";
    a[(std::size_t)StringId::UpdatesRestartNow]          = "Reiniciar ahora";
    a[(std::size_t)StringId::UpdatesRestartLater]        = "Reiniciar más tarde";
    a[(std::size_t)StringId::UpdatesDownloading]         = "Descargando...";
    a[(std::size_t)StringId::UpdatesFailed]              = "Actualización fallida";

    a[(std::size_t)StringId::LibraryEmptyTitle]          = "No hay juegos todavía";
    a[(std::size_t)StringId::LibraryEmptyHint]           =
        "Coloca ROMs en /foyer/roms/<sistema>/ en tu tarjeta SD.";
    a[(std::size_t)StringId::LibraryRecentlyPlayed]      = "Jugados recientemente";
    a[(std::size_t)StringId::LibraryFavorites]           = "Favoritos";
    a[(std::size_t)StringId::LibraryAllGames]            = "Todos los juegos";
    a[(std::size_t)StringId::LibrarySortBy]              = "Ordenar por";
    a[(std::size_t)StringId::LibrarySortAlphabetical]    = "Alfabético";
    a[(std::size_t)StringId::LibrarySortByGameCount]     = "Cantidad de juegos";
    a[(std::size_t)StringId::LibrarySortCustom]          = "Personalizado";

    a[(std::size_t)StringId::GamePlay]                   = "Jugar";
    a[(std::size_t)StringId::GameResume]                 = "Reanudar";
    a[(std::size_t)StringId::GameRestart]                = "Reiniciar";
    a[(std::size_t)StringId::GameAddToFavorites]         = "Agregar a favoritos";
    a[(std::size_t)StringId::GameRemoveFromFavorites]    = "Quitar de favoritos";
    a[(std::size_t)StringId::GameScrape]                 = "Buscar metadatos";
    a[(std::size_t)StringId::GamePickCover]              = "Elegir portada";
    a[(std::size_t)StringId::GameClearPlaytime]          = "Borrar tiempo jugado";
    a[(std::size_t)StringId::GameDelete]                 = "Borrar";

    a[(std::size_t)StringId::ScraperLibretroThumbnails]  = "libretro-thumbnails";
    a[(std::size_t)StringId::ScraperScreenscraper]       = "ScreenScraper";
    a[(std::size_t)StringId::ScraperSteamgriddb]         = "SteamGridDB";
    a[(std::size_t)StringId::ScraperPickCover]           = "Elegir portada";
    a[(std::size_t)StringId::ScraperNoMatches]           = "Sin coincidencias";

    a[(std::size_t)StringId::CoresInstall]               = "Instalar";
    a[(std::size_t)StringId::CoresUpdate]                = "Actualizar";
    a[(std::size_t)StringId::CoresDownloadFailed]        = "Descarga fallida";
    a[(std::size_t)StringId::CoresClearAllBezels]        = "Borrar todos los marcos";

    a[(std::size_t)StringId::Yes]                        = "Sí";
    a[(std::size_t)StringId::No]                         = "No";
    a[(std::size_t)StringId::OK]                         = "Aceptar";
    a[(std::size_t)StringId::Cancel]                     = "Cancelar";
    a[(std::size_t)StringId::Confirm]                    = "Confirmar";
    a[(std::size_t)StringId::Close]                      = "Cerrar";
    return a;
}();

} // namespace foyer::i18n
