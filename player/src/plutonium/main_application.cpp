#include "plutonium/main_application.hpp"
#include "libretro/frontend.hpp"
#include "libretro/shader.hpp"
#include "platform/log.hpp"
#include "plutonium/session_tracker.hpp"

#include <switch.h>
#include <cstdio>
#include <cstdlib>

namespace foyer::player::plut {

EmulatorLayout::EmulatorLayout() : Layout::Layout() {}

void EmulatorLayout::Rebuild(EmulatorElement::Ref emu,
                             pu::ui::elm::Rectangle::Ref dim,
                             pu::ui::elm::Menu::Ref menu,
                             bool paused) {
    this->Clear();
    this->Add(emu);
    if (paused) {
        if (dim)  this->Add(dim);
        if (menu) this->Add(menu);
    }
}

void MainApplication::SetBootArgs(std::string rp,
                                  std::string bn,
                                  std::string sys) {
    this->rom_path      = std::move(rp);
    this->back_nro      = std::move(bn);
    this->system_folder = std::move(sys);
}

void MainApplication::OnLoad() {
    this->emu_element = EmulatorElement::New();
    this->emu_layout  = EmulatorLayout::New();
    // 50% opacity black wash. Rectangle takes pu::ui::Color + i32
    // x,y,w,h. ScreenWidth/Height are 1920/1080 baseline.
    this->dim_rect = pu::ui::elm::Rectangle::New(
        0, 0,
        pu::ui::render::ScreenWidth, pu::ui::render::ScreenHeight,
        pu::ui::Color{ 0, 0, 0, 140 });
    this->emu_layout->Rebuild(this->emu_element, nullptr, nullptr, false);
    this->LoadLayout(this->emu_layout);

    if (!this->rom_path.empty()) {
        if (!this->emu_element->BootGame(this->rom_path,
                                         this->back_nro,
                                         this->system_folder)) {
            foyer::log::write(
                "[player-plutonium] BootGame failed; closing\n");
            this->Close();
            return;
        }
        // Bind the per-session writeback tracker to this rom and
        // capture per-game baselines before any pause-menu pick can
        // mutate the general config. finalize() fires from the Quit
        // cell in pause_menu.cpp before envSetNextLoad.
        SessionTracker::instance().start(this->emu_element->OriginalRomPath());
        this->pause = std::make_unique<PauseMenu>(
            this->emu_element->OriginalRomPath(),
            this->emu_element->SystemFolder(),
            this->emu_element->BackNro());
        this->pause->SetOnClose([this]() {
            this->paused = false;
            this->emu_element->SetPaused(false);
            SessionTracker::instance().set_paused(false);
            this->layout_dirty = true;
        });
        this->pause->SetOnQuit([this]() {
            // Quit teardown order matters — anything that touches
            // SDL or GL after Renderer::Finalize crashes (PC=0x30
            // on a freed vtable). Sequence:
            //   1. emu_element->Shutdown  — libretro unload + audio
            //      close + GL texture deletes while context is live
            //   2. shader_pipeline().release_borrowed  — drop the
            //      pipeline's GL state (programs / FBOs / VAO)
            //      while SDL's GL context is still current; the
            //      static ~ShaderPipeline would otherwise run at
            //      exit() AFTER SDL_Quit and crash
            //   3. emu_layout->Clear  — release Menu / Rectangle
            //      refs so their SDL_DestroyTexture calls run
            //      against a live renderer
            //   4. Close(false) sets is_shown=false + Finalize the
            //      renderer (no exit(0))
            //   5. exit(0) ourselves to skip the C++ destructor
            //      chain entirely; libnx hbloader picks up the
            //      envSetNextLoad set by the Quit cell before this
            this->emu_element->Shutdown();
            foyer::libretro::shader_pipeline().release_borrowed();
            this->emu_layout->Clear();
            this->Close(false);
            std::exit(0);
        });
        this->pause->SetOnModeChanged([this]() {
            this->layout_dirty = true;
        });
    } else {
        foyer::log::write("[player-plutonium] no rom in argv — idle\n");
    }

    this->SetOnInput([this](const u64 keys_down,
                            const u64 keys_up,
                            const u64 keys_held,
                            const pu::ui::TouchPoint /*tp*/) {
        // Apply any layout rebuild deferred from a previous frame's
        // Menu callback BEFORE this frame walks the elements.
        if (this->layout_dirty) {
            this->layout_dirty = false;
            this->RefreshLayout();
        }
        // L3+R3 toggle on the RISING edge of the combo so a held
        // input doesn't open/close repeatedly. We track edges by
        // checking the held mask against the previous frame.
        const bool l3 = (keys_held & HidNpadButton_StickL) != 0;
        const bool r3 = (keys_held & HidNpadButton_StickR) != 0;
        const bool combo_now    = (l3 && r3);
        const bool combo_before = (this->last_l3 && this->last_r3);
        this->last_l3 = l3;
        this->last_r3 = r3;
        if (combo_now && !combo_before
                && this->emu_element->IsBooted()) {
            this->TogglePause();
            return;
        }
        // B while paused: pop sub-mode -> root, or root -> close.
        if (this->paused && (keys_down & HidNpadButton_B)) {
            if (!this->pause->OnBack()) {
                this->paused = false;
                this->emu_element->SetPaused(false);
                SessionTracker::instance().set_paused(false);
            }
            this->layout_dirty = true;
            return;
        }
        if (!this->emu_element->IsBooted()
                && (keys_down & HidNpadButton_Plus)) {
            this->Close();
        }
    });
}

void MainApplication::TogglePause() {
    this->paused = !this->paused;
    this->emu_element->SetPaused(this->paused);
    SessionTracker::instance().set_paused(this->paused);
    if (this->paused) {
        this->pause->SetMode(PauseMode::Pause);
    }
    // Rebuild defers to next frame so we don't mutate emu_layout
    // while Plutonium is iterating it.
    this->layout_dirty = true;
    foyer::log::write("[player-plutonium] paused=%d mode=%d\n",
        (int)this->paused, (int)this->pause->GetMode());
}

void MainApplication::RefreshLayout() {
    pu::ui::elm::Menu::Ref menu = nullptr;
    if (this->paused && this->pause) {
        menu = this->pause->BuildMenu();
    }
    this->emu_layout->Rebuild(this->emu_element,
                              this->paused ? this->dim_rect : nullptr,
                              menu,
                              this->paused);
}

}  // namespace foyer::player::plut
