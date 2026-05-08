#pragma once

#include <borealis.hpp>
#include <string>
#include <string_view>

namespace foyer::browser {

// Detail screen for one game: cover art (placeholder for now —
// box art lookup wires in later), the game's display name, and a
// Play button that chain-launches the per-system player NRO.
//
// Constructed with the system folder + game path so we can re-look-
// up the System / Game pair from library_state when the user hits
// Play (avoiding a stale dangling reference if the activity is held
// across rescans).
class GameActivity : public brls::Activity {
public:
    GameActivity(std::string_view system_folder, std::string_view game_path);

    brls::View* createContentView() override;

private:
    std::string m_system_folder;
    std::string m_game_path;
};

}  // namespace foyer::browser
