#pragma once

#include <borealis.hpp>

#include <string>

namespace foyer::player {

// Lists the cheats parsed out of the rom's .cht. Each row is a
// BooleanCell — toggling persists via save_cheats_for and pushes
// the new state to the running core via apply_cheats_to_core.
class CheatsPickerActivity : public brls::Activity {
public:
    CheatsPickerActivity(std::string system_folder,
                         std::string rom_stem);

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::string m_system_folder;
    std::string m_rom_stem;
};

}  // namespace foyer::player
