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
    a[(std::size_t)StringId::LibraryHideEmpty]           = "Ocultar sistemas vacíos";
    a[(std::size_t)StringId::LibraryHideEmptyHint]       =
        "Oculta los sistemas cuya carpeta de roms no tiene juegos escaneables.";
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

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "Foyer";
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

    a[(std::size_t)StringId::EmuInstallAllCheats]        = "Instalar todos los paquetes de trucos";
    a[(std::size_t)StringId::EmuInstallAllCheatsHint]    =
        "Recorre cada paquete; omite los que ya están en la versión del manifiesto.";
    a[(std::size_t)StringId::EmuInstallShaders]          = "Instalar presets de shaders";
    a[(std::size_t)StringId::EmuInstallShadersHint]      =
        "Descarga el catálogo de foyer-shaders en /foyer/shaders/.";
    a[(std::size_t)StringId::EmuBezelPickHint]           =
        "Elige un PNG para superponer alrededor de la imagen. (ninguno) deja el sistema limpio.";
    a[(std::size_t)StringId::EmuBezelClearAll]           = "Borrar todos los marcos";
    a[(std::size_t)StringId::EmuBezelClearAllHint]       =
        "Quita el PNG de cada sistema para que la imagen se vea sin adornos. "
        "Los archivos del catálogo siguen instalados; vuelve a elegir por sistema para reaplicar.";
    a[(std::size_t)StringId::BezelForPrefix]             = "Marco para %s";
    a[(std::size_t)StringId::BezelNoneOption]            = "(ninguno — sin marco)";
    a[(std::size_t)StringId::DefaultCorePrefix]          = "Núcleo por defecto (%s)";

    a[(std::size_t)StringId::RunaheadTwoFrames]          = "2 cuadros";
    a[(std::size_t)StringId::RunaheadThreeFrames]        = "3 cuadros";
    a[(std::size_t)StringId::RunaheadFourFrames]         = "4 cuadros";

    a[(std::size_t)StringId::AccNotConfigured]           = "(no configurado — edita %s)";
    a[(std::size_t)StringId::AccNotInstalled]            = "no instalado";
    a[(std::size_t)StringId::AccEditedViaOsk]            = "Se edita con el teclado en pantalla.";

    a[(std::size_t)StringId::WorkerBackgroundRunning]    = "Trabajo en segundo plano en curso";
    a[(std::size_t)StringId::WorkerCancelHint]           =
        "Cancela la transferencia en el próximo callback.";

    a[(std::size_t)StringId::UpdatesEverythingUpToDate]  = "Todo está al día";
    a[(std::size_t)StringId::UpdatesUpdateEverything]    = "Actualizar todos los núcleos";
    a[(std::size_t)StringId::UpdatesLastJustNow]         = "Última: ahora mismo";
    a[(std::size_t)StringId::UpdatesRescrapeNow]         = "Volver a buscar ahora";
    a[(std::size_t)StringId::UpdatesRescrapeHint]        =
        "Refresca los manifiestos de cores / marcos / trucos.";
    a[(std::size_t)StringId::UpdatesScrapeAllSystems]    = "Buscar metadatos de todos los sistemas";
    a[(std::size_t)StringId::UpdatesScrapeAllSystemsHint]=
        "Recorre cada sistema con el scraper preferido.";

    a[(std::size_t)StringId::ExpRomsOverUsb]             = "Roms por USB";
    a[(std::size_t)StringId::ExpRomsOverUsbHint]         =
        "Activa libhaze MTP limitado a /foyer/roms.";
    a[(std::size_t)StringId::ExpAutoStartUsb]            = "Iniciar USB al arrancar";
    a[(std::size_t)StringId::ExpAutoStartUsbHint]        =
        "Evita tener que activarlo en cada arranque.";
    a[(std::size_t)StringId::ExpVerboseLog]              = "Registro detallado";
    a[(std::size_t)StringId::ExpVerboseLogHint]          =
        "Escribe diagnóstico extra en /foyer/data/log.txt.";

    a[(std::size_t)StringId::VerbAction]                 = "Acción";
    a[(std::size_t)StringId::VerbUpdate]                 = "Actualizar %s";
    a[(std::size_t)StringId::VerbInstall]                = "Instalar %s";
    a[(std::size_t)StringId::VerbSkipVersion]            = "Omitir esta versión";
    a[(std::size_t)StringId::VerbUpdateNow]              = "Actualizar ahora";
    a[(std::size_t)StringId::VerbReinstall]              = "Reinstalar";

    a[(std::size_t)StringId::UpdateAllAvailableCores]    = "Actualizar todos los núcleos disponibles";
    a[(std::size_t)StringId::UpdateAllAvailableCoresHint] =
        "Recorre cada núcleo marcado como actualización disponible y los descarga en orden.";

    a[(std::size_t)StringId::SearchTitlePrefix]          = "Buscar: ";
    a[(std::size_t)StringId::SearchTypeToSearch]         = "Escribe para buscar";
    a[(std::size_t)StringId::SearchPressYToEnter]        = "Pulsa Y para escribir una consulta";
    a[(std::size_t)StringId::SearchNoMatches]            = "Sin coincidencias";
    a[(std::size_t)StringId::SearchPressYToRefine]       = "Pulsa Y para refinar la búsqueda";

    a[(std::size_t)StringId::GridNoGames]                = "Sin juegos";
    a[(std::size_t)StringId::GridNoCover]                = "sin portada";

    a[(std::size_t)StringId::BannerCoverSaved]           = "Portada guardada para %s";
    a[(std::size_t)StringId::BannerClearedBezel]         = "Marco quitado de %s";
    a[(std::size_t)StringId::BannerSkippedItem]          = "Omitido %s";
    a[(std::size_t)StringId::BannerInstallingItem]       = "Instalando %s...";
    a[(std::size_t)StringId::BannerReinstallingItem]     = "Reinstalando %s...";
    a[(std::size_t)StringId::BannerUpdatingItem]         = "Actualizando %s...";

    a[(std::size_t)StringId::PickerCurrentMarker]        = "● actual";

    a[(std::size_t)StringId::NeverPlayed]                = "nunca jugado";
    a[(std::size_t)StringId::PlayedJustNow]              = "jugado ahora mismo";
    a[(std::size_t)StringId::PlayedMinAgo]               = "jugado hace %d min";
    a[(std::size_t)StringId::PlayedHrAgo]                = "jugado hace %d h";
    a[(std::size_t)StringId::PlayedDaysAgo]              = "jugado hace %d días";
    a[(std::size_t)StringId::PlayedWkAgo]                = "jugado hace %d sem";
    a[(std::size_t)StringId::PlayedMoAgo]                = "jugado hace %d meses";
    a[(std::size_t)StringId::NoPlaytime]                 = "sin tiempo de juego";
    a[(std::size_t)StringId::PlaytimeSec]                = "%d s";
    a[(std::size_t)StringId::PlaytimeMin]                = "%d min";
    a[(std::size_t)StringId::PlaytimeHr]                 = "%d h";
    a[(std::size_t)StringId::PlaytimeHrMin]              = "%d h %d min";

    a[(std::size_t)StringId::UpdatePromptTitle]          = "¿Actualizar %s antes de jugar?";
    a[(std::size_t)StringId::UpdatePromptHint]           = "La versión %s está disponible.";
    a[(std::size_t)StringId::UpdatePromptUpdate]         = "Actualizar";
    a[(std::size_t)StringId::UpdatePromptPlayAnyway]     = "Jugar ya";

    a[(std::size_t)StringId::BannerLibraryRescanned]        = "Biblioteca re-escaneada";
    a[(std::size_t)StringId::BannerManifestFetchFail]       = "Fallo al obtener manifiesto - revisa el log";
    a[(std::size_t)StringId::BannerCoresManifestFetchFail]  = "Fallo al obtener manifiesto de núcleos";
    a[(std::size_t)StringId::BannerCheatsManifestFetchFail] = "Fallo al obtener manifiesto de trucos";
    a[(std::size_t)StringId::BannerBezelsManifestFetchFail] = "Fallo al obtener manifiesto de marcos";
    a[(std::size_t)StringId::BannerShadersManifestFetchFail] = "Fallo al obtener manifiesto de shaders";
    a[(std::size_t)StringId::BannerFetchingShaderManifest]  = "Obteniendo manifiesto de shaders...";
    a[(std::size_t)StringId::BannerFetchingCheatsManifest]  = "Obteniendo manifiesto de trucos...";
    a[(std::size_t)StringId::BannerFetchingBezelsManifest]  = "Obteniendo manifiesto de marcos...";
    a[(std::size_t)StringId::BannerNoCoverCandidates]       = "Sin candidatos de portada";
    a[(std::size_t)StringId::BannerScrapeWorkerFailed]      = "El worker de scrape no inició";
    a[(std::size_t)StringId::BannerScrapeAlreadyRunning]    = "Ya hay un scrape en curso";
    a[(std::size_t)StringId::BannerScrapeNoCovers]          = "Scrape sin portadas - revisa el log";
    a[(std::size_t)StringId::BannerDownloadingFoyerUpdate]  = "Descargando actualización de foyer...";
    a[(std::size_t)StringId::BannerRescanning]              = "Re-escaneando biblioteca...";
    a[(std::size_t)StringId::BannerCheckingFoyerUpdate]     = "Buscando actualización de foyer...";
    a[(std::size_t)StringId::BannerNoRecentlyPlayed]        = "Sin juegos recientes";
    a[(std::size_t)StringId::BannerSetSteamgriddbApiKey]    =
        "Configura steamgriddb.api_key en accounts.jsonc primero";
    a[(std::size_t)StringId::BannerFetchingCovers]          = "Obteniendo candidatos de portada...";
    a[(std::size_t)StringId::BannerVirtualSystemReorderBlock] =
        "Recientes/Favoritos no pueden moverse";
    a[(std::size_t)StringId::BannerAlreadyAtEdge]           = "Ya está en el borde";
    a[(std::size_t)StringId::BannerSystemReordered]         = "Sistema reordenado";
    a[(std::size_t)StringId::BannerScrapeQueued]            =
        "Scrape en cola — corre en la próxima pasada";
    a[(std::size_t)StringId::BannerShaderOverrideCleared]   =
        "Override de shader borrado (usa el general)";
    a[(std::size_t)StringId::BannerRunaheadOverrideCleared] =
        "Override de run-ahead borrado (usa el general)";
    a[(std::size_t)StringId::BannerPerGameOverrideCleared]  = "Override por juego borrado";
    a[(std::size_t)StringId::BannerSortChanged]             = "Orden cambiado - re-escaneando...";
    a[(std::size_t)StringId::BannerRescraping]              = "Re-buscando manifiestos...";
    a[(std::size_t)StringId::BannerInstallingBezelPack]     = "Instalando paquete de marcos: %s...";
    a[(std::size_t)StringId::BannerCoresFailedCheckLog]     = "%d núcleo%s fallaron - revisa el log";
    a[(std::size_t)StringId::BannerBezelPacksFailed]        =
        "%d paquete%s de marcos fallaron - revisa el log";
    a[(std::size_t)StringId::BannerBezelPacksReady]         =
        "Marcos listos (%d nuevos, %d actualizados, %d omitidos)";
    a[(std::size_t)StringId::BannerCoreNotInManifest]       = "Núcleo no está en el manifiesto: %s";
    a[(std::size_t)StringId::BannerCoreNotInstalled]        = "Núcleo no instalado: foyer-%s.nro";

    a[(std::size_t)StringId::RestartNow]                    = "Reiniciar ahora";

    // Sweep v0.4.11
    a[(std::size_t)StringId::BannerAddedToFavorites]        = "Añadido a favoritos";
    a[(std::size_t)StringId::BannerRemovedFromFavorites]    = "Eliminado de favoritos";
    a[(std::size_t)StringId::BannerPerGameShaderSet]        = "Shader por juego: %s";
    a[(std::size_t)StringId::BannerPerGameRunaheadOff]      = "Run-ahead por juego: desactivado";
    a[(std::size_t)StringId::BannerPerGameRunaheadOneFrame] = "Run-ahead por juego: 1 frame";
    a[(std::size_t)StringId::BannerPerGameRunaheadNFrames]  = "Run-ahead por juego: %d frames";
    a[(std::size_t)StringId::BannerPerGameCoreSet]          = "Núcleo por juego: %s";
    a[(std::size_t)StringId::BannerSystemDefaultCoreSet]    = "Núcleo predeterminado del sistema: %s";
    a[(std::size_t)StringId::BannerThemeChanged]            = "Tema: %s";
    a[(std::size_t)StringId::BannerShaderChanged]           = "Shader: %s";
    a[(std::size_t)StringId::BannerRunaheadOff]             = "Run-ahead: desactivado";
    a[(std::size_t)StringId::BannerRunaheadOneFrame]        = "Run-ahead: %d frame";
    a[(std::size_t)StringId::BannerRunaheadNFrames]         = "Run-ahead: %d frames";
    a[(std::size_t)StringId::BannerCoverCacheCleared]       = "Caché de portadas borrado";
    a[(std::size_t)StringId::BannerScrapeBulkHint]          =
        "Abre un sistema y pulsa Y — extracción masiva en la siguiente pasada";
    a[(std::size_t)StringId::BannerFetchingCoresManifest]   = "Obteniendo manifiesto de núcleos...";
    a[(std::size_t)StringId::BannerInstallingCheatPacks]    = "Instalando todos los paquetes de trucos...";
    a[(std::size_t)StringId::BannerInstallingBezelPacks]    = "Instalando todos los paquetes de marcos...";
    a[(std::size_t)StringId::BannerInstallingCheatPack]     = "Instalando paquete de trucos: %s...";
    a[(std::size_t)StringId::BannerCancelling]              = "Cancelando...";
    a[(std::size_t)StringId::BannerUpdatingEverything]      = "Actualizando todos los núcleos...";
    a[(std::size_t)StringId::BannerDrillDownComingNextPass] =
        "Vista detallada llegará en la próxima versión";
    a[(std::size_t)StringId::BannerScraperFieldSaved]       = "%s guardado";

    a[(std::size_t)StringId::UpdateFoyerHintFull]           =
        "Descarga foyer.nro a /switch/foyer/foyer.nro.new — aplicado en el próximo arranque.";
    a[(std::size_t)StringId::UpdateRestartHintFull]         =
        "Reemplaza foyer.nro y reinicia. No se pierden las partidas guardadas.";
    a[(std::size_t)StringId::BucketBezels]                  = "Marcos";
    a[(std::size_t)StringId::BucketCheats]                  = "Trucos";
    a[(std::size_t)StringId::PickerActionTitle]             = "Acción";
    a[(std::size_t)StringId::PickerUpdateTitle]             = "Actualizar %s";
    a[(std::size_t)StringId::PickerInstallTitle]            = "Instalar %s";
    a[(std::size_t)StringId::PickerCandidatePrefix]         = "Candidato %d";
    a[(std::size_t)StringId::PickCoverForGame]              = "Elegir portada para %s";
    a[(std::size_t)StringId::SwkbdSearchGuide]              = "Buscar juegos por nombre";
    a[(std::size_t)StringId::GameDetailContinueLabel]       = "Continuar";

    a[(std::size_t)StringId::ShaderPrettyScanlines]         = "Líneas de barrido";
    a[(std::size_t)StringId::ShaderPrettyCrtSimple]         = "CRT (simple)";
    a[(std::size_t)StringId::ShaderPrettyLcdGrid]           = "Cuadrícula LCD";
    a[(std::size_t)StringId::ShaderPrettyGbDmg]             = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderPrettyGbaCorrect]        = "GBA color corregido";
    a[(std::size_t)StringId::ShaderPrettyNone]              = "Apagado";

    a[(std::size_t)StringId::ResumeQuickSlot]               = "ranura rápida";
    a[(std::size_t)StringId::ResumeSlotN]                   = "ranura %d";
    a[(std::size_t)StringId::ResumeBadge]                   = "reanudar";

    a[(std::size_t)StringId::DetailHeaderResumeAndCore]     = "Continuar / Núcleo";
    a[(std::size_t)StringId::DetailHeaderCoreOnly]          = "Núcleo";
    a[(std::size_t)StringId::KnobBadgePerGame]              = "por juego";
    a[(std::size_t)StringId::KnobBadgeDefault]              = "predeterminado";
    a[(std::size_t)StringId::DetailShaderRowLabel]          = "Shader";
    a[(std::size_t)StringId::DetailRunaheadRowLabel]        = "Run-ahead";
    a[(std::size_t)StringId::DetailRunaheadOff]             = "Apagado";
    a[(std::size_t)StringId::DetailRunaheadOneFrame]        = "1 frame";
    a[(std::size_t)StringId::DetailRunaheadNFrames]         = "%d frames";

    a[(std::size_t)StringId::HintNavigate]                  = "navegar";
    a[(std::size_t)StringId::HintBack]                      = "atrás";
    a[(std::size_t)StringId::HintQuit]                      = "salir";
    a[(std::size_t)StringId::HintMenu]                      = "menú";
    a[(std::size_t)StringId::HintSettings]                  = "ajustes";
    a[(std::size_t)StringId::HintPick]                      = "elegir";
    a[(std::size_t)StringId::HintEnter]                     = "entrar";
    a[(std::size_t)StringId::HintLaunch]                    = "iniciar";
    a[(std::size_t)StringId::HintDetails]                   = "detalles";
    a[(std::size_t)StringId::HintScrape]                    = "escanear";
    a[(std::size_t)StringId::HintToggle]                    = "alternar";
    a[(std::size_t)StringId::HintEdit]                      = "editar";
    a[(std::size_t)StringId::HintSelect]                    = "seleccionar";
    a[(std::size_t)StringId::HintChange]                    = "cambiar";
    a[(std::size_t)StringId::HintRun]                       = "ejecutar";
    a[(std::size_t)StringId::HintContinueVerb]              = "continuar";
    a[(std::size_t)StringId::HintCycleShader]               = "ciclar shader";
    a[(std::size_t)StringId::HintCycleRunAhead]             = "ciclar run-ahead";
    a[(std::size_t)StringId::HintClearOverride]             = "borrar anulación";
    a[(std::size_t)StringId::HintSetPerGame]                = "fijar por juego";
    a[(std::size_t)StringId::HintSetSysDefault]             = "fijar predet. del sistema";
    a[(std::size_t)StringId::HintNewQuery]                  = "nueva búsqueda";
    a[(std::size_t)StringId::HintOpen]                      = "abrir";

    a[(std::size_t)StringId::CountMatchSingular]            = "%zu coincidencia";
    a[(std::size_t)StringId::CountMatchPlural]              = "%zu coincidencias";
    a[(std::size_t)StringId::CountItemsPlural]              = "%zu elementos";
    a[(std::size_t)StringId::CountItemsWithMb]              = "%zu elementos   ~%.0f MB";
    a[(std::size_t)StringId::CountItemsWithKb]              = "%zu elementos   ~%llu KB";
    a[(std::size_t)StringId::CountAchievements]             = "%d / %d logros";
    a[(std::size_t)StringId::CountGameSingular]             = "juego";
    a[(std::size_t)StringId::CountGamePlural]               = "juegos";
    return a;
}();

} // namespace foyer::i18n
