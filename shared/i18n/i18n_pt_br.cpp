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
    return a;
}();

} // namespace foyer::i18n
