#include "imgui/modals.hpp"

#include "library/config.hpp"
#include "libretro/cheats.hpp"
#include "libretro/core_options.hpp"
#include "libretro/frontend.hpp"
#include "libretro/savestate.hpp"
#include "libretro/shader.hpp"
#include "libretro/video_gl.hpp"
#include "platform/log.hpp"

#include <imgui.h>
#include <switch.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

extern "C" void retro_reset(void);

namespace foyer::player::imgui_shell {

namespace {

Modal       g_modal           = Modal::None;
Modal       g_pending         = Modal::None;
void      (*g_on_quit)()      = nullptr;
bool        g_quit_requested  = false;
std::string g_back_nro;
std::string g_rom_path;
std::string g_system_folder;
std::string g_rom_stem;

void compute_rom_stem() {
    g_rom_stem = g_rom_path;
    if (const auto sl = g_rom_stem.find_last_of('/'); sl != std::string::npos)
        g_rom_stem = g_rom_stem.substr(sl + 1);
    if (const auto dot = g_rom_stem.find_last_of('.'); dot != std::string::npos)
        g_rom_stem = g_rom_stem.substr(0, dot);
}

void center_next_window(float w, float h) {
    const auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - w) * 0.5f,
                                   vp->Pos.y + (vp->Size.y - h) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
}

constexpr ImGuiWindowFlags kModalFlags =
    ImGuiWindowFlags_NoTitleBar    |
    ImGuiWindowFlags_NoResize      |
    ImGuiWindowFlags_NoMove        |
    ImGuiWindowFlags_NoSavedSettings;

bool select_row(const char* label, bool& confirmed) {
    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_None,
            ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 1.4f))) {
        confirmed = true;
        return true;
    }
    return false;
}

void apply_pending() {
    if (g_pending == g_modal) return;
    g_modal = g_pending;
}

// ----- Pause root --------------------------------------------------------

void draw_pause() {
    center_next_window(560.0f, 540.0f);
    ImGui::Begin("##pause", nullptr, kModalFlags);
    ImGui::Text("Paused");
    ImGui::Separator();

    bool clicked = false;
    if (select_row("Resume", clicked)) {
        g_pending = Modal::None;
    }
    if (select_row("Restart — reset the game", clicked)) {
        ::retro_reset();
        foyer::log::write("[modals] retro_reset\n");
        g_pending = Modal::None;
    }
    if (select_row("Save state — pick a slot", clicked)) {
        g_pending = Modal::SaveSlot;
    }
    if (select_row("Load state — pick a slot", clicked)) {
        g_pending = Modal::LoadSlot;
    }
    if (select_row("Core options", clicked)) {
        g_pending = Modal::CoreOptions;
    }
    if (select_row("Display — aspect / scale", clicked)) {
        g_pending = Modal::Display;
    }
    if (select_row("Shaders", clicked)) {
        g_pending = Modal::Shaders;
    }
    if (select_row("Cheats", clicked)) {
        g_pending = Modal::Cheats;
    }
    if (select_row("Quit — back to foyer", clicked)) {
        foyer::libretro::Frontend::instance().flush_sram();
        if (g_on_quit) g_on_quit();
        if (!g_back_nro.empty()) {
            const std::string sd = "sdmc:" + g_back_nro;
            char a[512]; std::snprintf(a, sizeof(a), "\"%s\"", sd.c_str());
            envSetNextLoad(sd.c_str(), a);
        }
        g_quit_requested = true;
        g_pending = Modal::None;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::None;
    }
    ImGui::End();
}

// ----- Slot picker (Save / Load) ----------------------------------------

void draw_slot(bool save_mode) {
    center_next_window(560.0f, 540.0f);
    ImGui::Begin("##slot", nullptr, kModalFlags);
    ImGui::Text(save_mode ? "Save state — pick a slot"
                          : "Load state — pick a slot");
    ImGui::Separator();

    foyer::libretro::StateSlot slots[foyer::libretro::kStateSlotCount];
    foyer::libretro::inspect_slots(g_rom_path, g_system_folder, slots);

    for (int i = 0; i < foyer::libretro::kStateSlotCount; ++i) {
        char label[128];
        if (slots[i].exists) {
            std::time_t t = slots[i].mtime;
            std::tm tm = *std::localtime(&t);
            char ts[32];
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
            std::snprintf(label, sizeof(label),
                "Slot %d — %s (%.1f KB)",
                i, ts, slots[i].size_bytes / 1024.0);
        } else {
            std::snprintf(label, sizeof(label), "Slot %d — empty", i);
        }
        bool clicked = false;
        if (select_row(label, clicked)) {
            const auto path = foyer::libretro::state_path_for(
                g_rom_path, g_system_folder, i);
            if (save_mode) {
                if (foyer::libretro::save_state(path)) {
                    foyer::log::write("[modals] saved slot %d -> %s\n",
                        i, path.c_str());
                } else {
                    foyer::log::write("[modals] save_state(%s) failed\n",
                        path.c_str());
                }
                g_pending = Modal::None;
            } else if (slots[i].exists) {
                if (foyer::libretro::load_state(path)) {
                    foyer::log::write("[modals] loaded slot %d <- %s\n",
                        i, path.c_str());
                    g_pending = Modal::None;
                } else {
                    foyer::log::write("[modals] load_state(%s) failed\n",
                        path.c_str());
                }
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Shaders picker ---------------------------------------------------

void draw_shaders() {
    center_next_window(560.0f, 540.0f);
    ImGui::Begin("##shaders", nullptr, kModalFlags);
    ImGui::Text("Shaders");
    ImGui::Separator();

    const auto presets = foyer::libretro::ShaderPipeline::available_presets();
    const std::string active = foyer::libretro::shader_pipeline().active();

    auto pick = [&](const std::string& name, const std::string& label) {
        char buf[160];
        if (name == active)
            std::snprintf(buf, sizeof(buf), "%s — Active", label.c_str());
        else
            std::snprintf(buf, sizeof(buf), "%s", label.c_str());
        bool clicked = false;
        if (select_row(buf, clicked)) {
            foyer::libretro::shader_pipeline().set_preset(name);
            foyer::library::set_shader_name(name);
            g_pending = Modal::Pause;
        }
    };

    pick("none", "None");
    for (const auto& p : presets) {
        if (p.name == "none") continue;
        pick(p.name, p.label.empty() ? p.name : p.label);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Display picker --------------------------------------------------

void draw_display() {
    using foyer::libretro::AspectMode;
    struct Row { const char* label; AspectMode m; };
    const Row rows[] = {
        { "Core default",   AspectMode::DisplayCore  },
        { "4:3",            AspectMode::Display43    },
        { "16:9",           AspectMode::Display169   },
        { "Stretch",        AspectMode::Stretch      },
        { "Integer 1x",     AspectMode::Integer1x    },
        { "Integer 2x",     AspectMode::Integer2x    },
        { "Integer auto",   AspectMode::IntegerAuto  },
    };

    center_next_window(560.0f, 540.0f);
    ImGui::Begin("##display", nullptr, kModalFlags);
    ImGui::Text("Display");
    ImGui::Separator();

    const auto cur = foyer::libretro::VideoSinkGl::instance().aspect();
    for (const auto& r : rows) {
        char buf[64];
        if (r.m == cur)
            std::snprintf(buf, sizeof(buf), "%s — Active", r.label);
        else
            std::snprintf(buf, sizeof(buf), "%s", r.label);
        bool clicked = false;
        if (select_row(buf, clicked)) {
            foyer::libretro::VideoSinkGl::instance().set_aspect(r.m);
            g_pending = Modal::Pause;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Core options picker --------------------------------------------

void draw_core_options() {
    center_next_window(700.0f, 540.0f);
    ImGui::Begin("##core_options", nullptr, kModalFlags);
    ImGui::Text("Core options");
    ImGui::Separator();

    auto& co = foyer::libretro::CoreOptions::instance();
    const auto& opts = co.options();
    if (opts.empty()) {
        ImGui::TextDisabled("(this core publishes no options)");
    } else {
        ImGui::BeginChild("##co_scroll", ImVec2(0, 460), false);
        for (const auto& o : opts) {
            char hdr[160];
            std::snprintf(hdr, sizeof(hdr), "%s = %s",
                (o.desc.empty() ? o.key : o.desc).c_str(),
                o.value.c_str());
            if (ImGui::TreeNode(hdr)) {
                for (const auto& v : o.choices) {
                    bool clicked = false;
                    char row[160];
                    std::snprintf(row, sizeof(row), "  %s%s",
                        v.c_str(), v == o.value ? " — Active" : "");
                    if (select_row(row, clicked)) {
                        co.set(o.key, v);
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Cheats picker ---------------------------------------------------

std::vector<foyer::libretro::Cheat> g_cheats_cache;
bool                                g_cheats_loaded = false;

void ensure_cheats_loaded() {
    if (g_cheats_loaded) return;
    g_cheats_cache = foyer::libretro::load_cheats_for(g_system_folder,
                                                     g_rom_stem);
    g_cheats_loaded = true;
}

void draw_cheats() {
    center_next_window(700.0f, 540.0f);
    ImGui::Begin("##cheats", nullptr, kModalFlags);
    ImGui::Text("Cheats");
    ImGui::Separator();

    ensure_cheats_loaded();
    if (g_cheats_cache.empty()) {
        ImGui::TextDisabled("(no cheats loaded for this game)");
    } else {
        ImGui::BeginChild("##ch_scroll", ImVec2(0, 460), false);
        for (std::size_t i = 0; i < g_cheats_cache.size(); ++i) {
            char buf[160];
            const bool on = g_cheats_cache[i].enabled;
            std::snprintf(buf, sizeof(buf), "%s %s",
                on ? "[ON]" : "[  ]",
                g_cheats_cache[i].desc.c_str());
            bool clicked = false;
            if (select_row(buf, clicked)) {
                g_cheats_cache[i].enabled = !on;
                foyer::libretro::save_cheats_for(
                    g_system_folder, g_rom_stem, g_cheats_cache);
                foyer::libretro::apply_cheats_to_core(g_cheats_cache);
            }
        }
        ImGui::EndChild();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

}  // namespace

bool modals_quit_requested() { return g_quit_requested; }

void modals_open_pause() {
    if (g_modal != Modal::None) return;
    g_pending = Modal::Pause;
    g_modal   = Modal::Pause;
    foyer::log::write("[modals] open Pause\n");
}

void modals_open(Modal m) { g_pending = m; }
void modals_close_all()   { g_pending = Modal::None; }
Modal modals_active()     { return g_modal; }
bool  modals_input_blocked() { return g_modal != Modal::None; }

void modals_draw() {
    apply_pending();
    switch (g_modal) {
        case Modal::None:        return;
        case Modal::Pause:       draw_pause();        break;
        case Modal::SaveSlot:    draw_slot(true);     break;
        case Modal::LoadSlot:    draw_slot(false);    break;
        case Modal::Shaders:     draw_shaders();      break;
        case Modal::Display:     draw_display();      break;
        case Modal::CoreOptions: draw_core_options(); break;
        case Modal::Cheats:      draw_cheats();       break;
    }
}

void modals_install_quit_handler(void (*on_quit)(),
                                 const std::string& back_nro) {
    g_on_quit = on_quit;
    g_back_nro = back_nro;
}

void modals_set_rom_id(const std::string& rom_path,
                       const std::string& system_folder) {
    g_rom_path      = rom_path;
    g_system_folder = system_folder;
    compute_rom_stem();
}

}  // namespace foyer::player::imgui_shell
