// Brazilian Portuguese (pt-BR) catalogue.
//
// Initial pass — translation pending native-speaker review. Slots
// left intentionally null fall through to English at lookup time, so
// partial translations are safe to ship.
//
// pt-BR specifically — uses Brazilian Portuguese vocabulary and
// orthography (e.g. "Tela" instead of European "Ecrã"; post-1990
// orthographic agreement spellings). A separate pt (European) slot
// can join later if needed.

#include "i18n.hpp"

#include <array>
#include <cstddef>

namespace foyer::i18n {

constexpr std::size_t kCount =
    static_cast<std::size_t>(StringId::kStringIdCount);

extern const std::array<const char*, kCount> kPortugueseBrazilStrings;

const std::array<const char*, kCount> kPortugueseBrazilStrings = [] {
    std::array<const char*, kCount> a{};
    a[(std::size_t)StringId::Home]                       = "Início";
    a[(std::size_t)StringId::Library]                    = "Biblioteca";
    a[(std::size_t)StringId::Settings]                   = "Configurações";
    a[(std::size_t)StringId::Search]                     = "Buscar";
    a[(std::size_t)StringId::Updates]                    = "Atualizações";
    a[(std::size_t)StringId::Back]                       = "Voltar";
    a[(std::size_t)StringId::Quit]                       = "Sair";

    a[(std::size_t)StringId::SettingsDisplay]            = "Tela";
    a[(std::size_t)StringId::SettingsEmulator]           = "Emulador";
    a[(std::size_t)StringId::SettingsLibrary]            = "Biblioteca";
    a[(std::size_t)StringId::SettingsScrapers]           = "Scrapers";
    a[(std::size_t)StringId::SettingsNetworking]         = "Rede";
    a[(std::size_t)StringId::SettingsAbout]              = "Sobre";

    a[(std::size_t)StringId::UpdatesCheckForUpdates]     = "Verificar atualizações";
    a[(std::size_t)StringId::UpdatesUpToDate]            = "Está atualizado";
    a[(std::size_t)StringId::UpdatesAvailable]           = "Atualização disponível";
    a[(std::size_t)StringId::UpdatesInstall]             = "Instalar";
    a[(std::size_t)StringId::UpdatesRestartNow]          = "Reiniciar agora";
    a[(std::size_t)StringId::UpdatesRestartLater]        = "Reiniciar depois";
    a[(std::size_t)StringId::UpdatesDownloading]         = "Baixando...";
    a[(std::size_t)StringId::UpdatesFailed]              = "Atualização falhou";

    a[(std::size_t)StringId::LibraryEmptyTitle]          = "Nenhum jogo ainda";
    a[(std::size_t)StringId::LibraryEmptyHint]           =
        "Coloque ROMs em /foyer/roms/<sistema>/ no seu cartão SD.";
    a[(std::size_t)StringId::LibraryRecentlyPlayed]      = "Jogados recentemente";
    a[(std::size_t)StringId::LibraryFavorites]           = "Favoritos";
    a[(std::size_t)StringId::LibraryAllGames]            = "Todos os jogos";
    a[(std::size_t)StringId::LibrarySortBy]              = "Ordenar por";
    a[(std::size_t)StringId::LibrarySortAlphabetical]    = "Alfabética";
    a[(std::size_t)StringId::LibrarySortByGameCount]     = "Quantidade de jogos";
    a[(std::size_t)StringId::LibrarySortCustom]          = "Personalizada";

    a[(std::size_t)StringId::GamePlay]                   = "Jogar";
    a[(std::size_t)StringId::GameResume]                 = "Continuar";
    a[(std::size_t)StringId::GameRestart]                = "Reiniciar";
    a[(std::size_t)StringId::GameAddToFavorites]         = "Adicionar aos favoritos";
    a[(std::size_t)StringId::GameRemoveFromFavorites]    = "Remover dos favoritos";
    a[(std::size_t)StringId::GameScrape]                 = "Buscar metadados";
    a[(std::size_t)StringId::GamePickCover]              = "Escolher capa";
    a[(std::size_t)StringId::GameClearPlaytime]          = "Apagar tempo de jogo";
    a[(std::size_t)StringId::GameDelete]                 = "Apagar";

    a[(std::size_t)StringId::ScraperLibretroThumbnails]  = "libretro-thumbnails";
    a[(std::size_t)StringId::ScraperScreenscraper]       = "ScreenScraper";
    a[(std::size_t)StringId::ScraperSteamgriddb]         = "SteamGridDB";
    a[(std::size_t)StringId::ScraperPickCover]           = "Escolher capa";
    a[(std::size_t)StringId::ScraperNoMatches]           = "Nenhum resultado";

    a[(std::size_t)StringId::CoresInstall]               = "Instalar";
    a[(std::size_t)StringId::CoresUpdate]                = "Atualizar";
    a[(std::size_t)StringId::CoresDownloadFailed]        = "Falha no download";
    a[(std::size_t)StringId::CoresClearAllBezels]        = "Apagar todas as molduras";

    a[(std::size_t)StringId::Yes]                        = "Sim";
    a[(std::size_t)StringId::No]                         = "Não";
    a[(std::size_t)StringId::OK]                         = "OK";
    a[(std::size_t)StringId::Cancel]                     = "Cancelar";
    a[(std::size_t)StringId::Confirm]                    = "Confirmar";
    a[(std::size_t)StringId::Close]                      = "Fechar";
    a[(std::size_t)StringId::Later]                      = "Depois";
    a[(std::size_t)StringId::Exit]                       = "Sair";

    a[(std::size_t)StringId::EmptyNoSystemsFound]        = "Nenhum sistema encontrado";
    a[(std::size_t)StringId::EmptyDropRomsHint]          =
        "coloque roms em /foyer/roms/<sistema>/ e escaneie de novo";
    a[(std::size_t)StringId::EmptyNoSystems]             = "Sem sistemas";
    a[(std::size_t)StringId::EmptyNoRomsInFolder]        = "nenhuma rom nesta pasta";
    a[(std::size_t)StringId::EmptyNoCoverScrapeHint]     = "sem capa (Y para buscar)";

    a[(std::size_t)StringId::GamePublisher]              = "Distribuidora";
    a[(std::size_t)StringId::GameDeveloper]              = "Desenvolvedora";
    a[(std::size_t)StringId::GamePlayers]                = "Jogadores";
    a[(std::size_t)StringId::GameRating]                 = "Classificação";

    a[(std::size_t)StringId::ActionResumeLast]           = "Continuar último";
    a[(std::size_t)StringId::ActionMoveUp]               = "Mover para cima";
    a[(std::size_t)StringId::ActionMoveDown]             = "Mover para baixo";
    a[(std::size_t)StringId::ActionRescanGames]          = "Escanear jogos";
    a[(std::size_t)StringId::ActionToggleFavorite]       = "Alternar favorito";
    a[(std::size_t)StringId::ActionPickCover]            = "Escolher capa...";
    a[(std::size_t)StringId::ActionFavoriteAll]          = "Favoritar tudo";
    a[(std::size_t)StringId::ActionClearAllFavorites]    = "Limpar favoritos";
    a[(std::size_t)StringId::ActionScrapeSystem]         = "Buscar metadados do sistema";

    a[(std::size_t)StringId::QuitConfirmTitle]           = "Sair do foyer?";
    a[(std::size_t)StringId::UpdateRestartTitle]         = "Reinicie o foyer para aplicar v";
    a[(std::size_t)StringId::UpdateRestartHint]          =
        "Substitui foyer.nro e relança. Nenhum save será perdido.";
    a[(std::size_t)StringId::UpdateFoyerTitle]           = "Atualizar foyer para v";
    a[(std::size_t)StringId::UpdateFoyerHint]            =
        "Baixa foyer.nro para /switch/foyer/foyer.nro.new — aplicado no próximo boot.";

    a[(std::size_t)StringId::SettingsGeneral]            = "Geral";
    a[(std::size_t)StringId::SettingsAudio]              = "Áudio";
    a[(std::size_t)StringId::SettingsAccounts]           = "Contas";
    a[(std::size_t)StringId::SettingsExperimental]       = "Experimental";
    a[(std::size_t)StringId::SettingsPreferredScraper]   = "Scraper preferido";
    a[(std::size_t)StringId::SettingsPreferredScraperHint] =
        "Provedor usado quando Y busca um jogo.";
    a[(std::size_t)StringId::SettingsRomRoot]            = "Pasta de roms";
    a[(std::size_t)StringId::SettingsRomRootHint]        = "Onde o foyer escaneia as roms.";
    a[(std::size_t)StringId::SettingsScanSubfolders]     = "Escanear subpastas";
    a[(std::size_t)StringId::SettingsScanSubfoldersHint] = "Percorrer subdiretórios ao escanear.";
    a[(std::size_t)StringId::SettingsTheme]              = "Tema";
    a[(std::size_t)StringId::SettingsThemeHint]          = "Paleta e papel de parede ativos.";
    a[(std::size_t)StringId::SettingsShowClock]          = "Mostrar relógio";
    a[(std::size_t)StringId::SettingsShowClockHint]      = "Relógio na barra superior.";
    a[(std::size_t)StringId::SettingsLanguage]           = "Idioma";
    a[(std::size_t)StringId::SettingsLanguageHint]       =
        "Substitui o idioma do sistema. Reinicie para efeito completo.";

    a[(std::size_t)StringId::DisplayShowBackgrounds]     = "Mostrar planos de fundo";
    a[(std::size_t)StringId::DisplayShowBackgroundsHint] = "Plano de fundo por jogo na visão do sistema.";
    a[(std::size_t)StringId::DisplayShowCovers]          = "Mostrar capas";
    a[(std::size_t)StringId::DisplayShowCoversHint]      = "Capas na grade de jogos.";
    a[(std::size_t)StringId::DisplayShowBezels]          = "Mostrar molduras";
    a[(std::size_t)StringId::DisplayShowBezelsHint]      = "Sobrepõe um PNG por sistema ao redor da imagem.";
    a[(std::size_t)StringId::DisplayShader]              = "Shader";
    a[(std::size_t)StringId::DisplayShaderHint]          = "Filtro pós-processamento aplicado por quadro.";
    a[(std::size_t)StringId::DisplayRunahead]            = "Run-ahead";
    a[(std::size_t)StringId::DisplayRunaheadHint]        = "Reduz a latência emulando quadros adiantados.";
    a[(std::size_t)StringId::ShaderNone]                 = "Nenhum";
    a[(std::size_t)StringId::ShaderScanlines]            = "Linhas de varredura";
    a[(std::size_t)StringId::ShaderCrtSimple]            = "CRT (simples)";
    a[(std::size_t)StringId::ShaderLcdGrid]              = "Grade LCD";
    a[(std::size_t)StringId::ShaderGbDmg]                = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderGbaCorrect]           = "Correção GBA";
    a[(std::size_t)StringId::RunaheadOff]                = "Desligado";
    a[(std::size_t)StringId::RunaheadOneFrame]           = "1 quadro";
    a[(std::size_t)StringId::RunaheadNFrames]            = "%d quadros";

    a[(std::size_t)StringId::AudioSystemNote]            =
        "O volume do sistema é ajustado pelo menu Home do Switch";
    a[(std::size_t)StringId::AudioSystemNoteHint]        =
        "As opções de áudio por core ficam no menu de pausa do jogo.";

    a[(std::size_t)StringId::LibraryRescan]              = "Re-escanear biblioteca";
    a[(std::size_t)StringId::LibraryRescanHint]          = "Percorre /foyer/roms/ e refaz o cache.";
    a[(std::size_t)StringId::LibraryInvalidateCovers]    = "Invalidar cache de capas";
    a[(std::size_t)StringId::LibraryInvalidateCoversHint] = "Recarrega as capas a partir do disco.";
    a[(std::size_t)StringId::LibrarySortGames]           = "Ordenar jogos por";
    a[(std::size_t)StringId::LibrarySortGamesHint]       = "Ordem da grade de jogos por sistema.";
    a[(std::size_t)StringId::LibrarySortSystems]         = "Ordenar sistemas por";
    a[(std::size_t)StringId::LibrarySortSystemsHint]     = "Ordem dos blocos do carrossel inicial.";
    a[(std::size_t)StringId::SortByName]                 = "Nome";
    a[(std::size_t)StringId::SortByRecent]               = "Jogados recentemente";
    a[(std::size_t)StringId::SortByPlaytime]             = "Tempo de jogo";
    a[(std::size_t)StringId::SortByFavoritesFirst]       = "Favoritos primeiro";
    a[(std::size_t)StringId::SystemSortScannerOrder]     = "Ordem do scanner";
    a[(std::size_t)StringId::SystemSortAlphabetical]     = "Alfabética";
    a[(std::size_t)StringId::SystemSortGameCount]        = "Quantidade de jogos";
    a[(std::size_t)StringId::SystemSortCustom]           = "Personalizada";

    a[(std::size_t)StringId::EmuDefaultCore]             = "Core padrão por sistema";
    a[(std::size_t)StringId::EmuDefaultCoreHint]         = "configurar";
    a[(std::size_t)StringId::EmuCoresCatalog]            = "Catálogo de cores";
    a[(std::size_t)StringId::EmuBezelPacks]              = "Pacotes de molduras";
    a[(std::size_t)StringId::EmuCheatPacks]              = "Pacotes de cheats";
    a[(std::size_t)StringId::EmuShaderPresets]           = "Presets de shaders";
    a[(std::size_t)StringId::EmuStandalones]             = "Emuladores standalone externos";
    a[(std::size_t)StringId::EmuStandalonesHint]         = "Status PSP / GC";
    a[(std::size_t)StringId::EmuBezelPerSystem]          = "Moldura por sistema";
    a[(std::size_t)StringId::EmuBezelPerSystemHint]      = "escolher ou limpar";
    a[(std::size_t)StringId::EmuRefreshManifest]         = "Atualizar manifesto";
    a[(std::size_t)StringId::EmuLoadingCatalog]          = "Carregando catálogo...";
    a[(std::size_t)StringId::EmuInstallAllBezels]        = "Instalar todos os pacotes de molduras";
    a[(std::size_t)StringId::EmuInstallAllBezelsHint]    =
        "Percorre cada pacote; pula os que já estão na versão do manifesto.";
    a[(std::size_t)StringId::EmuRefreshHintCores]        =
        "Baixa a última lista da release foyer-cores do GitHub.";
    a[(std::size_t)StringId::EmuRefreshHintBezels]       = "Baixa a última lista de foyer-bezels.";
    a[(std::size_t)StringId::EmuRefreshHintCheats]       = "Baixa a última lista de foyer-cheats.";
    a[(std::size_t)StringId::EmuRefreshHintShaders]      = "Baixa a última lista de foyer-shaders.";

    a[(std::size_t)StringId::VerbDownload]               = "baixar";
    a[(std::size_t)StringId::VerbInstalled]              = "instalado";
    a[(std::size_t)StringId::VerbInstalledReinstall]     = "instalado - reinstalar";
    a[(std::size_t)StringId::VerbUpdateAvailable]        = "atualização disponível";
    a[(std::size_t)StringId::VerbFetch]                  = "buscar";
    a[(std::size_t)StringId::VerbRun]                    = "executar";
    a[(std::size_t)StringId::VerbRefresh]                = "atualizar";
    a[(std::size_t)StringId::VerbConfigure]              = "configurar";
    a[(std::size_t)StringId::VerbBrowseInstall]          = "ver / instalar";
    a[(std::size_t)StringId::VerbPickOrClear]            = "escolher ou limpar";
    a[(std::size_t)StringId::VerbStatusInfo]             = "Status PSP / GC";

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "foyer";
    a[(std::size_t)StringId::UpdatesCores]               = "Cores";
    a[(std::size_t)StringId::UpdatesBezels]              = "Molduras";
    a[(std::size_t)StringId::UpdatesCheats]              = "Cheats";
    a[(std::size_t)StringId::UpdatesShaders]             = "Shaders";
    a[(std::size_t)StringId::UpdatesCheckAll]            = "Verificar atualizações";
    a[(std::size_t)StringId::UpdatesCheckAllHint]        = "Atualiza todos os manifestos numa só varredura.";
    a[(std::size_t)StringId::UpdatesUpToDateCores]       = "Todos os cores estão atualizados";
    a[(std::size_t)StringId::UpdatesNewCores]            = "%d novos";
    a[(std::size_t)StringId::UpdatesUpdatedCores]        = "%d atualizados";
    a[(std::size_t)StringId::UpdatesFailedCores]         = "%d falharam";
    a[(std::size_t)StringId::UpdatesInstalling]          = "Instalando %s...";
    a[(std::size_t)StringId::UpdatesScanning]            = "Escaneando...";
    a[(std::size_t)StringId::UpdatesNoData]              = "Sem dados — atualize primeiro";
    a[(std::size_t)StringId::UpdatesFetchManifest]       = "Buscando manifesto...";

    a[(std::size_t)StringId::AccScreenscraperDevId]      = "ID dev do ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperDevPwd]     = "Senha dev do ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperUser]       = "Usuário do ScreenScraper";
    a[(std::size_t)StringId::AccScreenscraperPwd]        = "Senha do ScreenScraper";
    a[(std::size_t)StringId::AccSteamgriddbApiKey]       = "Chave API do SteamGridDB";
    a[(std::size_t)StringId::AccRetroachUser]            = "Usuário do RetroAchievements";
    a[(std::size_t)StringId::AccRetroachToken]           = "Token API do RetroAchievements";
    a[(std::size_t)StringId::AccLoginRequired]           = "Necessário para usar este scraper";
    a[(std::size_t)StringId::AccCleared]                 = "Limpo";

    a[(std::size_t)StringId::PickCoverTitle]             = "Escolher capa";
    a[(std::size_t)StringId::PickCoverHint]              = "A escolher   B cancelar   Direcional navegar";
    a[(std::size_t)StringId::PickerCancel]               = "Cancelar";
    a[(std::size_t)StringId::PickerNoResults]            = "Sem capas";
    a[(std::size_t)StringId::PickerLoading]              = "Carregando...";

    a[(std::size_t)StringId::BannerInstalling]           = "Instalando %s...";
    a[(std::size_t)StringId::BannerDone]                 = "%s pronto";
    a[(std::size_t)StringId::BannerFailed]               = "%s falhou";
    a[(std::size_t)StringId::BannerCancelled]            = "%s cancelado";

    a[(std::size_t)StringId::SearchPlaceholder]          = "Buscar...";
    a[(std::size_t)StringId::SearchEmptyHint]            = "Digite para filtrar a biblioteca";
    a[(std::size_t)StringId::SearchNoResults]            = "Sem resultados";

    a[(std::size_t)StringId::LangEnglish]                = "Inglês";
    a[(std::size_t)StringId::LangSpanish]                = "Espanhol";
    a[(std::size_t)StringId::LangPortugueseBrazil]       = "Português (Brasil)";
    a[(std::size_t)StringId::LangSystemDefault]          = "Padrão do sistema";
    return a;
}();

} // namespace foyer::i18n
