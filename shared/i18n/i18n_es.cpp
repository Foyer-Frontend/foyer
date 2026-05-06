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
    a[(std::size_t)StringId::Later]                      = "Después";
    a[(std::size_t)StringId::Exit]                       = "Salir";

    a[(std::size_t)StringId::EmptyNoSystemsFound]        = "No hay sistemas";
    a[(std::size_t)StringId::EmptyDropRomsHint]          =
        "coloca roms en /foyer/roms/<sistema>/ y vuelve a escanear";
    a[(std::size_t)StringId::EmptyNoSystems]             = "Sin sistemas";
    a[(std::size_t)StringId::EmptyNoRomsInFolder]        = "no hay roms en esta carpeta";
    a[(std::size_t)StringId::EmptyNoCoverScrapeHint]     = "sin portada (Y para buscar)";

    a[(std::size_t)StringId::GamePublisher]              = "Distribuidor";
    a[(std::size_t)StringId::GameDeveloper]              = "Desarrollador";
    a[(std::size_t)StringId::GamePlayers]                = "Jugadores";
    a[(std::size_t)StringId::GameRating]                 = "Clasificación";

    a[(std::size_t)StringId::ActionResumeLast]           = "Reanudar último";
    a[(std::size_t)StringId::ActionMoveUp]               = "Subir";
    a[(std::size_t)StringId::ActionMoveDown]             = "Bajar";
    a[(std::size_t)StringId::ActionRescanGames]          = "Escanear juegos";
    a[(std::size_t)StringId::ActionToggleFavorite]       = "Alternar favorito";
    a[(std::size_t)StringId::ActionPickCover]            = "Elegir portada...";
    a[(std::size_t)StringId::ActionFavoriteAll]          = "Marcar todos como favoritos";
    a[(std::size_t)StringId::ActionClearAllFavorites]    = "Quitar todos los favoritos";
    a[(std::size_t)StringId::ActionScrapeSystem]         = "Buscar metadatos del sistema";

    a[(std::size_t)StringId::QuitConfirmTitle]           = "¿Salir de foyer?";
    a[(std::size_t)StringId::UpdateRestartTitle]         = "Reinicia foyer para aplicar v";
    a[(std::size_t)StringId::UpdateRestartHint]          =
        "Reemplaza foyer.nro y se relanza. No se pierden datos guardados.";
    a[(std::size_t)StringId::UpdateFoyerTitle]           = "Actualizar foyer a v";
    a[(std::size_t)StringId::UpdateFoyerHint]            =
        "Descarga foyer.nro a /switch/foyer/foyer.nro.new — se aplica al próximo arranque.";

    a[(std::size_t)StringId::SettingsGeneral]            = "General";
    a[(std::size_t)StringId::SettingsAudio]              = "Audio";
    a[(std::size_t)StringId::SettingsAccounts]           = "Cuentas";
    a[(std::size_t)StringId::SettingsExperimental]       = "Experimental";
    a[(std::size_t)StringId::SettingsPreferredScraper]   = "Scraper preferido";
    a[(std::size_t)StringId::SettingsPreferredScraperHint] =
        "Proveedor usado al pulsar Y para buscar.";
    a[(std::size_t)StringId::SettingsRomRoot]            = "Carpeta de roms";
    a[(std::size_t)StringId::SettingsRomRootHint]        = "Dónde foyer escanea las roms.";
    a[(std::size_t)StringId::SettingsScanSubfolders]     = "Escanear subcarpetas";
    a[(std::size_t)StringId::SettingsScanSubfoldersHint] = "Recorrer subdirectorios al escanear.";
    a[(std::size_t)StringId::SettingsTheme]              = "Tema";
    a[(std::size_t)StringId::SettingsThemeHint]          = "Paleta y fondo activos.";
    a[(std::size_t)StringId::SettingsShowClock]          = "Mostrar reloj";
    a[(std::size_t)StringId::SettingsShowClockHint]      = "Reloj en la barra superior.";
    a[(std::size_t)StringId::SettingsLanguage]           = "Idioma";
    a[(std::size_t)StringId::SettingsLanguageHint]       =
        "Anula el idioma del sistema. Reinicia para que surta pleno efecto.";

    a[(std::size_t)StringId::DisplayShowBackgrounds]     = "Mostrar fondos";
    a[(std::size_t)StringId::DisplayShowBackgroundsHint] = "Imagen de fondo por juego en la vista del sistema.";
    a[(std::size_t)StringId::DisplayShowCovers]          = "Mostrar portadas";
    a[(std::size_t)StringId::DisplayShowCoversHint]      = "Carátulas en la cuadrícula de juegos.";
    a[(std::size_t)StringId::DisplayShowBezels]          = "Mostrar marcos";
    a[(std::size_t)StringId::DisplayShowBezelsHint]      = "Superpone un PNG por sistema alrededor de la imagen.";
    a[(std::size_t)StringId::DisplayShader]              = "Shader";
    a[(std::size_t)StringId::DisplayShaderHint]          = "Filtro post-proceso aplicado por cuadro.";
    a[(std::size_t)StringId::DisplayRunahead]            = "Run-ahead";
    a[(std::size_t)StringId::DisplayRunaheadHint]        = "Reduce la latencia ejecutando cuadros adelantados.";
    a[(std::size_t)StringId::ShaderNone]                 = "Ninguno";
    a[(std::size_t)StringId::ShaderScanlines]            = "Líneas de barrido";
    a[(std::size_t)StringId::ShaderCrtSimple]            = "CRT (simple)";
    a[(std::size_t)StringId::ShaderLcdGrid]              = "Rejilla LCD";
    a[(std::size_t)StringId::ShaderGbDmg]                = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderGbaCorrect]           = "Corrección GBA";
    a[(std::size_t)StringId::RunaheadOff]                = "Apagado";
    a[(std::size_t)StringId::RunaheadOneFrame]           = "1 cuadro";
    a[(std::size_t)StringId::RunaheadNFrames]            = "%d cuadros";

    a[(std::size_t)StringId::AudioSystemNote]            =
        "El volumen del sistema se ajusta desde el menú Home de Switch";
    a[(std::size_t)StringId::AudioSystemNoteHint]        =
        "Los ajustes de audio por núcleo están en el menú de pausa del juego.";

    a[(std::size_t)StringId::LibraryRescan]              = "Volver a escanear biblioteca";
    a[(std::size_t)StringId::LibraryRescanHint]          = "Recorre /foyer/roms/ y reconstruye la caché.";
    a[(std::size_t)StringId::LibraryInvalidateCovers]    = "Invalidar caché de portadas";
    a[(std::size_t)StringId::LibraryInvalidateCoversHint] = "Vuelve a leer las carátulas desde el disco.";
    a[(std::size_t)StringId::LibrarySortGames]           = "Ordenar juegos por";
    a[(std::size_t)StringId::LibrarySortGamesHint]       = "Orden de la cuadrícula de juegos por sistema.";
    a[(std::size_t)StringId::LibrarySortSystems]         = "Ordenar sistemas por";
    a[(std::size_t)StringId::LibrarySortSystemsHint]     = "Orden de las viñetas del carrusel principal.";
    a[(std::size_t)StringId::SortByName]                 = "Nombre";
    a[(std::size_t)StringId::SortByRecent]               = "Jugados recientemente";
    a[(std::size_t)StringId::SortByPlaytime]             = "Tiempo jugado";
    a[(std::size_t)StringId::SortByFavoritesFirst]       = "Favoritos primero";
    a[(std::size_t)StringId::SystemSortScannerOrder]     = "Orden del escáner";
    a[(std::size_t)StringId::SystemSortAlphabetical]     = "Alfabético";
    a[(std::size_t)StringId::SystemSortGameCount]        = "Cantidad de juegos";
    a[(std::size_t)StringId::SystemSortCustom]           = "Personalizado";

    a[(std::size_t)StringId::EmuDefaultCore]             = "Núcleo por defecto por sistema";
    a[(std::size_t)StringId::EmuDefaultCoreHint]         = "configurar";
    a[(std::size_t)StringId::EmuCoresCatalog]            = "Catálogo de núcleos";
    a[(std::size_t)StringId::EmuBezelPacks]              = "Paquetes de marcos";
    a[(std::size_t)StringId::EmuCheatPacks]              = "Paquetes de trucos";
    a[(std::size_t)StringId::EmuShaderPresets]           = "Presets de shaders";
    a[(std::size_t)StringId::EmuStandalones]             = "Emuladores standalone externos";
    a[(std::size_t)StringId::EmuStandalonesHint]         = "Estado PSP / GC";
    a[(std::size_t)StringId::EmuBezelPerSystem]          = "Marco por sistema";
    a[(std::size_t)StringId::EmuBezelPerSystemHint]      = "elegir o quitar";
    a[(std::size_t)StringId::EmuRefreshManifest]         = "Actualizar manifiesto";
    a[(std::size_t)StringId::EmuLoadingCatalog]          = "Cargando catálogo...";
    a[(std::size_t)StringId::EmuInstallAllBezels]        = "Instalar todos los paquetes de marcos";
    a[(std::size_t)StringId::EmuInstallAllBezelsHint]    =
        "Recorre cada paquete; omite los que ya están en la versión del manifiesto.";
    a[(std::size_t)StringId::EmuRefreshHintCores]        =
        "Descarga el último listado de la release foyer-cores desde GitHub.";
    a[(std::size_t)StringId::EmuRefreshHintBezels]       = "Descarga el último listado de foyer-bezels.";
    a[(std::size_t)StringId::EmuRefreshHintCheats]       = "Descarga el último listado de foyer-cheats.";
    a[(std::size_t)StringId::EmuRefreshHintShaders]      = "Descarga el último listado de foyer-shaders.";

    a[(std::size_t)StringId::VerbDownload]               = "descargar";
    a[(std::size_t)StringId::VerbInstalled]              = "instalado";
    a[(std::size_t)StringId::VerbInstalledReinstall]     = "instalado - reinstalar";
    a[(std::size_t)StringId::VerbUpdateAvailable]        = "actualización disponible";
    a[(std::size_t)StringId::VerbFetch]                  = "obtener";
    a[(std::size_t)StringId::VerbRun]                    = "ejecutar";
    a[(std::size_t)StringId::VerbRefresh]                = "refrescar";
    a[(std::size_t)StringId::VerbConfigure]              = "configurar";
    a[(std::size_t)StringId::VerbBrowseInstall]          = "ver / instalar";
    a[(std::size_t)StringId::VerbPickOrClear]            = "elegir o quitar";
    a[(std::size_t)StringId::VerbStatusInfo]             = "Estado PSP / GC";

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "foyer";
    a[(std::size_t)StringId::UpdatesCores]               = "Núcleos";
    a[(std::size_t)StringId::UpdatesBezels]              = "Marcos";
    a[(std::size_t)StringId::UpdatesCheats]              = "Trucos";
    a[(std::size_t)StringId::UpdatesShaders]             = "Shaders";
    a[(std::size_t)StringId::UpdatesCheckAll]            = "Buscar actualizaciones";
    a[(std::size_t)StringId::UpdatesCheckAllHint]        = "Refresca cada manifiesto en una pasada.";
    a[(std::size_t)StringId::UpdatesUpToDateCores]       = "Todos los núcleos están al día";
    a[(std::size_t)StringId::UpdatesNewCores]            = "%d nuevos";
    a[(std::size_t)StringId::UpdatesUpdatedCores]        = "%d actualizados";
    a[(std::size_t)StringId::UpdatesFailedCores]         = "%d fallaron";
    a[(std::size_t)StringId::UpdatesInstalling]          = "Instalando %s...";
    a[(std::size_t)StringId::UpdatesScanning]            = "Escaneando...";
    a[(std::size_t)StringId::UpdatesNoData]              = "Sin datos — refresca primero";
    a[(std::size_t)StringId::UpdatesFetchManifest]       = "Obteniendo manifiesto...";

    a[(std::size_t)StringId::AccScreenscraperDevId]      = "ID dev de ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperDevPwd]     = "Contraseña dev de ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperUser]       = "Usuario de ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperPwd]        = "Contraseña de ScreenScraper";
    a[(std::size_t)StringId::AccSteamgriddbApiKey]       = "Clave API de SteamGridDB";
    a[(std::size_t)StringId::AccRetroachUser]            = "Usuario de RetroAchievements";
    a[(std::size_t)StringId::AccRetroachToken]           = "Token API de RetroAchievements";
    a[(std::size_t)StringId::AccLoginRequired]           = "Requerido para usar este scraper";
    a[(std::size_t)StringId::AccCleared]                 = "Borrado";

    a[(std::size_t)StringId::PickCoverTitle]             = "Elegir portada";
    a[(std::size_t)StringId::PickCoverHint]              = "A elegir   B cancelar   Cruceta navegar";
    a[(std::size_t)StringId::PickerCancel]               = "Cancelar";
    a[(std::size_t)StringId::PickerNoResults]            = "Sin portadas";
    a[(std::size_t)StringId::PickerLoading]              = "Cargando...";

    a[(std::size_t)StringId::BannerInstalling]           = "Instalando %s...";
    a[(std::size_t)StringId::BannerDone]                 = "%s listo";
    a[(std::size_t)StringId::BannerFailed]               = "%s falló";
    a[(std::size_t)StringId::BannerCancelled]            = "%s cancelado";

    a[(std::size_t)StringId::SearchPlaceholder]          = "Buscar...";
    a[(std::size_t)StringId::SearchEmptyHint]            = "Escribe para filtrar la biblioteca";
    a[(std::size_t)StringId::SearchNoResults]            = "Sin resultados";

    a[(std::size_t)StringId::LangEnglish]                = "Inglés";
    a[(std::size_t)StringId::LangSpanish]                = "Español";
    a[(std::size_t)StringId::LangPortugueseBrazil]       = "Portugués (Brasil)";
    a[(std::size_t)StringId::LangSystemDefault]          = "Predeterminado del sistema";
    return a;
}();

} // namespace foyer::i18n
