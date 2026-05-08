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
    a[(std::size_t)StringId::DisplayRoundedTiles]        = "Cantos arredondados";
    a[(std::size_t)StringId::DisplayRoundedTilesHint]    = "Suaviza os cantos das telas do carrossel.";
    a[(std::size_t)StringId::DisplayActionRowDock]       = "Dock de ações";
    a[(std::size_t)StringId::DisplayActionRowDockHint]   = "Agrupa os botões da Home em uma única pílula arredondada.";
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
    a[(std::size_t)StringId::LibraryHideEmpty]           = "Ocultar sistemas vazios";
    a[(std::size_t)StringId::LibraryHideEmptyHint]       =
        "Esconde sistemas cuja pasta de roms não tem jogos escaneáveis.";
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

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "Foyer";
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

    a[(std::size_t)StringId::EmuInstallAllCheats]        = "Instalar todos os pacotes de cheats";
    a[(std::size_t)StringId::EmuInstallAllCheatsHint]    =
        "Percorre cada pacote; pula os que já estão na versão do manifesto.";
    a[(std::size_t)StringId::EmuInstallShaders]          = "Instalar presets de shaders";
    a[(std::size_t)StringId::EmuInstallShadersHint]      =
        "Baixa o catálogo do foyer-shaders em /foyer/shaders/.";
    a[(std::size_t)StringId::EmuBezelPickHint]           =
        "Escolha um PNG para sobrepor à imagem. (nenhum) deixa o sistema limpo.";
    a[(std::size_t)StringId::EmuBezelClearAll]           = "Limpar todas as molduras";
    a[(std::size_t)StringId::EmuBezelClearAllHint]       =
        "Remove o PNG de cada sistema para a imagem aparecer sem moldura. "
        "Os arquivos do catálogo continuam instalados; escolha de novo por sistema para reaplicar.";
    a[(std::size_t)StringId::BezelForPrefix]             = "Moldura para %s";
    a[(std::size_t)StringId::BezelNoneOption]            = "(nenhuma — sem moldura)";
    a[(std::size_t)StringId::DefaultCorePrefix]          = "Core padrão (%s)";

    a[(std::size_t)StringId::RunaheadTwoFrames]          = "2 quadros";
    a[(std::size_t)StringId::RunaheadThreeFrames]        = "3 quadros";
    a[(std::size_t)StringId::RunaheadFourFrames]         = "4 quadros";

    a[(std::size_t)StringId::AccNotConfigured]           = "(não configurado — edite %s)";
    a[(std::size_t)StringId::AccNotInstalled]            = "não instalado";
    a[(std::size_t)StringId::AccEditedViaOsk]            = "Editado pelo teclado virtual.";

    a[(std::size_t)StringId::WorkerBackgroundRunning]    = "Tarefa em segundo plano em andamento";
    a[(std::size_t)StringId::WorkerCancelHint]           =
        "Cancela a transferência no próximo callback.";

    a[(std::size_t)StringId::UpdatesEverythingUpToDate]  = "Tudo está atualizado";
    a[(std::size_t)StringId::UpdatesUpdateEverything]    = "Atualizar todos os cores";
    a[(std::size_t)StringId::UpdatesLastJustNow]         = "Última: agora mesmo";
    a[(std::size_t)StringId::UpdatesRescrapeNow]         = "Buscar novamente";
    a[(std::size_t)StringId::UpdatesRescrapeHint]        =
        "Atualiza os manifestos de cores / molduras / cheats.";
    a[(std::size_t)StringId::UpdatesScrapeAllSystems]    = "Buscar metadados de todos os sistemas";
    a[(std::size_t)StringId::UpdatesScrapeAllSystemsHint]=
        "Percorre cada sistema usando o scraper preferido.";

    a[(std::size_t)StringId::ExpRomsOverUsb]             = "Roms por USB";
    a[(std::size_t)StringId::ExpRomsOverUsbHint]         =
        "Inicia o libhaze MTP limitado a /foyer/roms.";
    a[(std::size_t)StringId::ExpAutoStartUsb]            = "Iniciar USB no boot";
    a[(std::size_t)StringId::ExpAutoStartUsbHint]        =
        "Evita ter que ligar o USB toda vez que abrir o foyer.";
    a[(std::size_t)StringId::ExpVerboseLog]              = "Log detalhado";
    a[(std::size_t)StringId::ExpVerboseLogHint]          =
        "Grava diagnósticos extras em /foyer/data/log.txt.";

    a[(std::size_t)StringId::VerbAction]                 = "Ação";
    a[(std::size_t)StringId::VerbUpdate]                 = "Atualizar %s";
    a[(std::size_t)StringId::VerbInstall]                = "Instalar %s";
    a[(std::size_t)StringId::VerbSkipVersion]            = "Pular esta versão";
    a[(std::size_t)StringId::VerbUpdateNow]              = "Atualizar agora";
    a[(std::size_t)StringId::VerbReinstall]              = "Reinstalar";

    a[(std::size_t)StringId::UpdateAllAvailableCores]    = "Atualizar todos os cores disponíveis";
    a[(std::size_t)StringId::UpdateAllAvailableCoresHint] =
        "Percorre cada core marcado como atualização disponível e baixa em ordem.";

    a[(std::size_t)StringId::SearchTitlePrefix]          = "Buscar: ";
    a[(std::size_t)StringId::SearchTypeToSearch]         = "Digite para buscar";
    a[(std::size_t)StringId::SearchPressYToEnter]        = "Pressione Y para digitar uma busca";
    a[(std::size_t)StringId::SearchNoMatches]            = "Nenhum resultado";
    a[(std::size_t)StringId::SearchPressYToRefine]       = "Pressione Y para refinar a busca";

    a[(std::size_t)StringId::GridNoGames]                = "Sem jogos";
    a[(std::size_t)StringId::GridNoCover]                = "sem capa";

    a[(std::size_t)StringId::BannerCoverSaved]           = "Capa salva para %s";
    a[(std::size_t)StringId::BannerClearedBezel]         = "Moldura removida de %s";
    a[(std::size_t)StringId::BannerSkippedItem]          = "Pulado %s";
    a[(std::size_t)StringId::BannerInstallingItem]       = "Instalando %s...";
    a[(std::size_t)StringId::BannerReinstallingItem]     = "Reinstalando %s...";
    a[(std::size_t)StringId::BannerUpdatingItem]         = "Atualizando %s...";

    a[(std::size_t)StringId::PickerCurrentMarker]        = "● atual";

    a[(std::size_t)StringId::NeverPlayed]                = "nunca jogado";
    a[(std::size_t)StringId::PlayedJustNow]              = "jogado agora";
    a[(std::size_t)StringId::PlayedMinAgo]               = "jogado há %d min";
    a[(std::size_t)StringId::PlayedHrAgo]                = "jogado há %d h";
    a[(std::size_t)StringId::PlayedDaysAgo]              = "jogado há %d dias";
    a[(std::size_t)StringId::PlayedWkAgo]                = "jogado há %d sem";
    a[(std::size_t)StringId::PlayedMoAgo]                = "jogado há %d meses";
    a[(std::size_t)StringId::NoPlaytime]                 = "sem tempo de jogo";
    a[(std::size_t)StringId::PlaytimeSec]                = "%d s";
    a[(std::size_t)StringId::PlaytimeMin]                = "%d min";
    a[(std::size_t)StringId::PlaytimeHr]                 = "%d h";
    a[(std::size_t)StringId::PlaytimeHrMin]              = "%d h %d min";

    a[(std::size_t)StringId::UpdatePromptTitle]          = "Atualizar %s antes de jogar?";
    a[(std::size_t)StringId::UpdatePromptHint]           = "A versão %s está disponível.";
    a[(std::size_t)StringId::UpdatePromptUpdate]         = "Atualizar";
    a[(std::size_t)StringId::UpdatePromptPlayAnyway]     = "Jogar mesmo assim";

    a[(std::size_t)StringId::BannerLibraryRescanned]        = "Biblioteca reescaneada";
    a[(std::size_t)StringId::BannerManifestFetchFail]       = "Falha ao buscar manifesto - veja o log";
    a[(std::size_t)StringId::BannerCoresManifestFetchFail]  = "Falha ao buscar manifesto de cores";
    a[(std::size_t)StringId::BannerCheatsManifestFetchFail] = "Falha ao buscar manifesto de cheats";
    a[(std::size_t)StringId::BannerBezelsManifestFetchFail] = "Falha ao buscar manifesto de molduras";
    a[(std::size_t)StringId::BannerShadersManifestFetchFail] = "Falha ao buscar manifesto de shaders";
    a[(std::size_t)StringId::BannerFetchingShaderManifest]  = "Buscando manifesto de shaders...";
    a[(std::size_t)StringId::BannerFetchingCheatsManifest]  = "Buscando manifesto de cheats...";
    a[(std::size_t)StringId::BannerFetchingBezelsManifest]  = "Buscando manifesto de molduras...";
    a[(std::size_t)StringId::BannerNoCoverCandidates]       = "Sem candidatos de capa";
    a[(std::size_t)StringId::BannerScrapeWorkerFailed]      = "Worker de scrape não iniciou";
    a[(std::size_t)StringId::BannerScrapeAlreadyRunning]    = "Já há um scrape em andamento";
    a[(std::size_t)StringId::BannerScrapeNoCovers]          = "Scrape sem capas - veja o log";
    a[(std::size_t)StringId::BannerDownloadingFoyerUpdate]  = "Baixando atualização do foyer...";
    a[(std::size_t)StringId::BannerRescanning]              = "Reescaneando biblioteca...";
    a[(std::size_t)StringId::BannerCheckingFoyerUpdate]     = "Verificando atualização do foyer...";
    a[(std::size_t)StringId::BannerNoRecentlyPlayed]        = "Sem jogos recentes";
    a[(std::size_t)StringId::BannerSetSteamgriddbApiKey]    =
        "Configure steamgriddb.api_key em accounts.jsonc primeiro";
    a[(std::size_t)StringId::BannerFetchingCovers]          = "Buscando candidatos de capa...";
    a[(std::size_t)StringId::BannerVirtualSystemReorderBlock] =
        "Recentes/Favoritos não podem ser movidos";
    a[(std::size_t)StringId::BannerAlreadyAtEdge]           = "Já está na borda";
    a[(std::size_t)StringId::BannerSystemReordered]         = "Sistema reordenado";
    a[(std::size_t)StringId::BannerScrapeQueued]            =
        "Scrape em fila — roda na próxima passagem";
    a[(std::size_t)StringId::BannerShaderOverrideCleared]   =
        "Override de shader limpo (usa o geral)";
    a[(std::size_t)StringId::BannerRunaheadOverrideCleared] =
        "Override de run-ahead limpo (usa o geral)";
    a[(std::size_t)StringId::BannerPerGameOverrideCleared]  = "Override por jogo limpo";
    a[(std::size_t)StringId::BannerSortChanged]             = "Ordem alterada - reescaneando...";
    a[(std::size_t)StringId::BannerRescraping]              = "Re-buscando manifestos...";
    a[(std::size_t)StringId::BannerInstallingBezelPack]     = "Instalando pacote de molduras: %s...";
    a[(std::size_t)StringId::BannerCoresFailedCheckLog]     = "%d core%s falharam - veja o log";
    a[(std::size_t)StringId::BannerBezelPacksFailed]        =
        "%d pacote%s de molduras falharam - veja o log";
    a[(std::size_t)StringId::BannerBezelPacksReady]         =
        "Molduras prontas (%d novas, %d atualizadas, %d puladas)";
    a[(std::size_t)StringId::BannerCoreNotInManifest]       = "Core não está no manifesto: %s";
    a[(std::size_t)StringId::BannerCoreNotInstalled]        = "Core não instalado: foyer-%s.nro";

    a[(std::size_t)StringId::RestartNow]                    = "Reiniciar agora";

    // Sweep v0.4.11
    a[(std::size_t)StringId::BannerAddedToFavorites]        = "Adicionado aos favoritos";
    a[(std::size_t)StringId::BannerRemovedFromFavorites]    = "Removido dos favoritos";
    a[(std::size_t)StringId::BannerPerGameShaderSet]        = "Shader por jogo: %s";
    a[(std::size_t)StringId::BannerPerGameRunaheadOff]      = "Run-ahead por jogo: desligado";
    a[(std::size_t)StringId::BannerPerGameRunaheadOneFrame] = "Run-ahead por jogo: 1 frame";
    a[(std::size_t)StringId::BannerPerGameRunaheadNFrames]  = "Run-ahead por jogo: %d frames";
    a[(std::size_t)StringId::BannerPerGameCoreSet]          = "Core por jogo: %s";
    a[(std::size_t)StringId::BannerSystemDefaultCoreSet]    = "Core padrão do sistema: %s";
    a[(std::size_t)StringId::BannerThemeChanged]            = "Tema: %s";
    a[(std::size_t)StringId::BannerShaderChanged]           = "Shader: %s";
    a[(std::size_t)StringId::BannerRunaheadOff]             = "Run-ahead: desligado";
    a[(std::size_t)StringId::BannerRunaheadOneFrame]        = "Run-ahead: %d frame";
    a[(std::size_t)StringId::BannerRunaheadNFrames]         = "Run-ahead: %d frames";
    a[(std::size_t)StringId::BannerCoverCacheCleared]       = "Cache de capas limpo";
    a[(std::size_t)StringId::BannerScrapeBulkHint]          =
        "Abra um sistema e pressione Y — coleta em massa na próxima passagem";
    a[(std::size_t)StringId::BannerFetchingCoresManifest]   = "Obtendo manifesto de cores...";
    a[(std::size_t)StringId::BannerInstallingCheatPacks]    = "Instalando todos os pacotes de cheats...";
    a[(std::size_t)StringId::BannerInstallingBezelPacks]    = "Instalando todos os pacotes de molduras...";
    a[(std::size_t)StringId::BannerInstallingCheatPack]     = "Instalando pacote de cheats: %s...";
    a[(std::size_t)StringId::BannerCancelling]              = "Cancelando...";
    a[(std::size_t)StringId::BannerUpdatingEverything]      = "Atualizando todos os cores...";
    a[(std::size_t)StringId::BannerDrillDownComingNextPass] =
        "Visão detalhada virá na próxima versão";
    a[(std::size_t)StringId::BannerScraperFieldSaved]       = "%s salvo";

    a[(std::size_t)StringId::UpdateFoyerHintFull]           =
        "Baixa foyer.nro para /switch/foyer/foyer.nro.new — aplicado na próxima inicialização.";
    a[(std::size_t)StringId::UpdateRestartHintFull]         =
        "Substitui foyer.nro e reinicia. Saves no SD não são perdidos.";
    a[(std::size_t)StringId::BucketBezels]                  = "Molduras";
    a[(std::size_t)StringId::BucketCheats]                  = "Cheats";
    a[(std::size_t)StringId::PickerActionTitle]             = "Ação";
    a[(std::size_t)StringId::PickerUpdateTitle]             = "Atualizar %s";
    a[(std::size_t)StringId::PickerInstallTitle]            = "Instalar %s";
    a[(std::size_t)StringId::PickerCandidatePrefix]         = "Candidato %d";
    a[(std::size_t)StringId::PickCoverForGame]              = "Escolher capa para %s";
    a[(std::size_t)StringId::SwkbdSearchGuide]              = "Buscar jogos por nome";
    a[(std::size_t)StringId::GameDetailContinueLabel]       = "Continuar";

    a[(std::size_t)StringId::ShaderPrettyScanlines]         = "Scanlines";
    a[(std::size_t)StringId::ShaderPrettyCrtSimple]         = "CRT (simples)";
    a[(std::size_t)StringId::ShaderPrettyLcdGrid]           = "Grade LCD";
    a[(std::size_t)StringId::ShaderPrettyGbDmg]             = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderPrettyGbaCorrect]        = "GBA cor corrigida";
    a[(std::size_t)StringId::ShaderPrettyNone]              = "Desligado";

    a[(std::size_t)StringId::ResumeQuickSlot]               = "slot rápido";
    a[(std::size_t)StringId::ResumeSlotN]                   = "slot %d";
    a[(std::size_t)StringId::ResumeBadge]                   = "continuar";

    a[(std::size_t)StringId::DetailHeaderResumeAndCore]     = "Continuar / Core";
    a[(std::size_t)StringId::DetailHeaderCoreOnly]          = "Core";
    a[(std::size_t)StringId::KnobBadgePerGame]              = "por jogo";
    a[(std::size_t)StringId::KnobBadgeDefault]              = "padrão";
    a[(std::size_t)StringId::DetailShaderRowLabel]          = "Shader";
    a[(std::size_t)StringId::DetailRunaheadRowLabel]        = "Run-ahead";
    a[(std::size_t)StringId::DetailRunaheadOff]             = "Desligado";
    a[(std::size_t)StringId::DetailRunaheadOneFrame]        = "1 frame";
    a[(std::size_t)StringId::DetailRunaheadNFrames]         = "%d frames";

    a[(std::size_t)StringId::HintNavigate]                  = "navegar";
    a[(std::size_t)StringId::HintBack]                      = "voltar";
    a[(std::size_t)StringId::HintQuit]                      = "sair";
    a[(std::size_t)StringId::HintMenu]                      = "menu";
    a[(std::size_t)StringId::HintSettings]                  = "ajustes";
    a[(std::size_t)StringId::HintPick]                      = "escolher";
    a[(std::size_t)StringId::HintEnter]                     = "entrar";
    a[(std::size_t)StringId::HintLaunch]                    = "jogar";
    a[(std::size_t)StringId::HintDetails]                   = "detalhes";
    a[(std::size_t)StringId::HintScrape]                    = "buscar";
    a[(std::size_t)StringId::HintToggle]                    = "alternar";
    a[(std::size_t)StringId::HintEdit]                      = "editar";
    a[(std::size_t)StringId::HintSelect]                    = "selecionar";
    a[(std::size_t)StringId::HintChange]                    = "mudar";
    a[(std::size_t)StringId::HintRun]                       = "executar";
    a[(std::size_t)StringId::HintContinueVerb]              = "continuar";
    a[(std::size_t)StringId::HintCycleShader]               = "ciclar shader";
    a[(std::size_t)StringId::HintCycleRunAhead]             = "ciclar run-ahead";
    a[(std::size_t)StringId::HintClearOverride]             = "limpar override";
    a[(std::size_t)StringId::HintSetPerGame]                = "definir por jogo";
    a[(std::size_t)StringId::HintSetSysDefault]             = "definir padrão do sistema";
    a[(std::size_t)StringId::HintNewQuery]                  = "nova busca";
    a[(std::size_t)StringId::HintOpen]                      = "abrir";

    a[(std::size_t)StringId::CountMatchSingular]            = "%zu resultado";
    a[(std::size_t)StringId::CountMatchPlural]              = "%zu resultados";
    a[(std::size_t)StringId::CountItemsPlural]              = "%zu itens";
    a[(std::size_t)StringId::CountItemsWithMb]              = "%zu itens   ~%.0f MB";
    a[(std::size_t)StringId::CountItemsWithKb]              = "%zu itens   ~%llu KB";
    a[(std::size_t)StringId::CountAchievements]             = "%d / %d conquistas";
    a[(std::size_t)StringId::CountGameSingular]             = "jogo";
    a[(std::size_t)StringId::CountGamePlural]               = "jogos";

    a[(std::size_t)StringId::BootStarting]                  = "Iniciando...";
    a[(std::size_t)StringId::BootSeedingAssets]             = "Preparando recursos...";
    a[(std::size_t)StringId::BootInitNetwork]               = "Inicializando rede...";
    a[(std::size_t)StringId::BootLoadingTheme]              = "Carregando tema...";
    a[(std::size_t)StringId::BootScanningLibrary]           = "Escaneando biblioteca...";
    a[(std::size_t)StringId::BootReady]                     = "Pronto";

    a[(std::size_t)StringId::CoreTagPerGame]                = "por jogo";
    a[(std::size_t)StringId::CoreTagSystemDefault]          = "padrão do sistema";
    a[(std::size_t)StringId::CoreTagActive]                 = "ativo";
    a[(std::size_t)StringId::CoreTagBuiltInDefault]         = "padrão integrado";

    a[(std::size_t)StringId::ActionPastSkipped]             = "ignorado";
    a[(std::size_t)StringId::ActionPastUpdated]             = "atualizado";
    a[(std::size_t)StringId::ActionPastInstalled]           = "instalado";
    a[(std::size_t)StringId::ActionPastFailed]              = "FALHOU";

    a[(std::size_t)StringId::AccountUnset]                  = "não definido";
    a[(std::size_t)StringId::UpdatesMetadataKindLabel]      = "metadados";

    a[(std::size_t)StringId::DetailBezelRowLabel]           = "Moldura";
    a[(std::size_t)StringId::DetailCoreRowLabel]            = "Core";
    a[(std::size_t)StringId::DetailUseSystemDefault]        = "(padrão do sistema)";
    a[(std::size_t)StringId::DetailValueNone]               = "(nenhuma)";
    a[(std::size_t)StringId::DetailUseSystemDefaultPicker]  = "Padrão do sistema";
    a[(std::size_t)StringId::BannerPerGameBezelSet]         = "Moldura por jogo: %s";
    a[(std::size_t)StringId::BannerPerGameBezelCleared]     = "Moldura por jogo removida";
    return a;
}();

} // namespace foyer::i18n
