#pragma once

#include <borealis.hpp>

#include <string>

namespace foyer::player {

// Modal slot picker pushed on top of PauseActivity when the user
// taps Save state or Load state. Lists Quick (slot 0) + slots
// 1..9, each row showing whether the slot has data on disk plus
// its mtime so the user knows what they're overwriting / loading
// from.
class SlotPickerActivity : public brls::Activity {
public:
    enum class Mode { Save, Load };

    SlotPickerActivity(Mode mode,
                       std::string rom_path,
                       std::string system_folder);

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    Mode         m_mode;
    std::string  m_rom_path;
    std::string  m_system_folder;
};

}  // namespace foyer::player
