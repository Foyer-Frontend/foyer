#pragma once
//
// Plutonium Element that owns the libretro tick. Each OnInput drives
// Frontend::run_frame; each OnRender composites the current game
// frame + bezel onto the SDL renderer.

#include "libretro/aspect.hpp"
#include "libretro/cheevos.hpp"

#include <SDL2/SDL.h>
#include <switch.h>
#include <pu/Plutonium>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>

namespace foyer::player::plut {

class EmulatorElement : public pu::ui::elm::Element {
public:
    EmulatorElement();
    PU_SMART_CTOR(EmulatorElement)
    ~EmulatorElement() override;

    bool BootGame(const std::string& rom_path,
                  const std::string& back_nro,
                  const std::string& system_folder);

    void Shutdown();

    // Element overrides — full-screen by definition.
    pu::i32 GetX() override { return 0; }
    pu::i32 GetY() override { return 0; }
    pu::i32 GetWidth()  override { return pu::ui::render::ScreenWidth; }
    pu::i32 GetHeight() override { return pu::ui::render::ScreenHeight; }
    void OnRender(pu::ui::render::Renderer::Ref& drawer,
                  const pu::i32 x, const pu::i32 y) override;
    void OnInput(const u64 keys_down, const u64 keys_up,
                 const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    bool IsBooted() const { return m_game_ok; }

    // Pause toggle — gates run_frame in OnInput. Drawing keeps
    // happening, so the last frame stays on-screen behind the
    // pause menu.
    void SetPaused(bool p) { m_paused = p; }
    bool IsPaused() const  { return m_paused; }

    // Aspect mode hook for the picker modals (Phase P4).
    void SetAspect(foyer::libretro::AspectMode m);

    // Accessors for MainApplication's pause-menu setup.
    const std::string& OriginalRomPath() const { return m_original_rom_path; }
    const std::string& BackNro()         const { return m_back_nro; }
    const std::string& SystemFolder()    const { return m_system_folder; }

    // Queue an in-game toast — drawn in OnRender as a top-right
    // pill that fades out after ~5 s. Used by the RetroAchievements
    // unlock callback so the user sees the trigger without leaving
    // the rom. Thread-safe; rcheevos's event handler runs on the
    // libretro frame thread which is the same as our OnInput, but
    // pause-time async (e.g. server callbacks) may differ.
    void PushToast(const foyer::libretro::Cheevos::Unlock& ev);

private:
    struct Toast {
        std::string                                          title;
        std::string                                          subtitle;   // "<points> pts" or empty
        std::string                                          badge_url;
        SDL_Texture*                                         badge   = nullptr;  // lazily loaded
        bool                                                 fetched = false;     // worker has started or finished
        std::chrono::steady_clock::time_point                ts;
    };

    bool        m_game_ok       = false;
    bool        m_paused        = false;
    bool        m_pad_inited    = false;
    PadState    m_pad{};
    std::string m_rom_path;
    std::string m_original_rom_path;
    std::string m_back_nro;
    std::string m_system_folder;

    std::mutex         m_toast_mu;
    std::deque<Toast>  m_toasts;
};

}  // namespace foyer::player::plut
