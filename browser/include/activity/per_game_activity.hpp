#pragma once

#include <borealis.hpp>

#include <string>
#include <string_view>

namespace foyer::browser {

// Per-game settings overlay pushed from SystemActivity (+ button
// on a tile) or GameActivity (+ button on the details view).
// Surfaces the knobs from foyer::library::per_game:
//   - Core override per rom
//   - Shader pick per rom
//   - Runahead frames per rom
//   - Favourite toggle
// Each cell persists immediately via the per_game.jsonc writers.
class PerGameActivity : public brls::Activity {
public:
    PerGameActivity(std::string_view system_folder,
                    std::string_view rom_path);
    ~PerGameActivity() override = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::string m_system_folder;
    std::string m_rom_path;
};

}  // namespace foyer::browser
