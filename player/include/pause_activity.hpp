#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

namespace foyer::player {

// Overlay Activity pushed when the user triggers the pause menu
// (L3+R3 in EmulatorActivity). Lists every per-game knob — save /
// load state, display, shaders, cheats, core options — plus
// Resume and Quit.
//
// rom_path / system_folder are the *original* paths the launcher
// handed us, not the extracted .nes / .smc; save / load state
// route their .state slot files relative to those so the file
// layout stays stable across .zip vs raw-rom launches.
class PauseActivity : public brls::Activity {
public:
    using QuitCallback = std::function<void()>;

    PauseActivity(std::string rom_path,
                  std::string system_folder,
                  std::string back_nro,
                  QuitCallback on_quit);

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::string  m_rom_path;
    std::string  m_system_folder;
    std::string  m_back_nro;
    QuitCallback m_on_quit;
};

}  // namespace foyer::player
