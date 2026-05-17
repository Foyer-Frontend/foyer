#include "imgui/modals.hpp"
#include "imgui/icons.hpp"

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

constexpr float kFontScale  = 1.6f;   // 13pt default -> ~21pt
constexpr float kRowPadY    = 12.0f;
constexpr ImU32 kDimColor   = IM_COL32(0, 0, 0, 160);

// Full-screen translucent dimmer drawn behind every modal. Honours
// the theme indirectly through ImGui's foreground draw list.
void draw_dim() {
    const auto vp = ImGui::GetMainViewport();
    auto* dl = ImGui::GetBackgroundDrawList(vp);
    dl->AddRectFilled(vp->Pos,
                      ImVec2(vp->Pos.x + vp->Size.x,
                             vp->Pos.y + vp->Size.y),
                      kDimColor);
}

void center_next_window(float w, float h) {
    const auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - w) * 0.5f,
                                   vp->Pos.y + (vp->Size.y - h) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
}

constexpr ImGuiWindowFlags kModalFlags =
    ImGuiWindowFlags_NoTitleBar    |
    ImGuiWindowFlags_NoResize      |
    ImGuiWindowFlags_NoMove        |
    ImGuiWindowFlags_NoSavedSettings;

void begin_modal(const char* title) {
    draw_dim();
    ImGui::Begin("##modal", nullptr, kModalFlags);
    ImGui::SetWindowFontScale(kFontScale);
    // Auto-focus the first item so gamepad nav can drive it
    // immediately. ImGui keeps the focus after the first frame.
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
}

bool select_row(const char* icon, const char* label) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "  %s   %s", icon, label);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(8.0f, kRowPadY));
    const bool clicked = ImGui::Selectable(buf, false,
        ImGuiSelectableFlags_None,
        ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 1.6f));
    ImGui::PopStyleVar();
    return clicked;
}

void apply_pending() {
    if (g_pending == g_modal) return;
    g_modal = g_pending;
}

// ----- Pause root --------------------------------------------------------

void draw_pause() {
    center_next_window(640.0f, 620.0f);
    begin_modal("Paused");

    if (select_row(ICON_FA_PLAY,        "Resume"))         g_pending = Modal::None;
    if (select_row(ICON_FA_ROTATE,      "Restart")) {
        ::retro_reset();
        foyer::log::write("[modals] retro_reset\n");
        g_pending = Modal::None;
    }
    if (select_row(ICON_FA_FLOPPY,      "Save state"))     g_pending = Modal::SaveSlot;
    if (select_row(ICON_FA_FOLDER_OPEN, "Load state"))     g_pending = Modal::LoadSlot;
    if (select_row(ICON_FA_GEAR,        "Core options"))   g_pending = Modal::CoreOptions;
    if (select_row(ICON_FA_DESKTOP,     "Display"))        g_pending = Modal::Display;
    if (select_row(ICON_FA_WAND,        "Shaders"))        g_pending = Modal::Shaders;
    if (select_row(ICON_FA_BUG,         "Cheats"))         g_pending = Modal::Cheats;
    if (select_row(ICON_FA_POWER,       "Quit to foyer")) {
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

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        g_pending = Modal::None;
    }
    ImGui::End();
}

// ----- Slot picker (Save / Load) ----------------------------------------

void draw_slot(bool save_mode) {
    center_next_window(720.0f, 620.0f);
    begin_modal(save_mode ? "Save state" : "Load state");

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
                "Slot %d   %s   %.1f KB",
                i, ts, slots[i].size_bytes / 1024.0);
        } else {
            std::snprintf(label, sizeof(label), "Slot %d   (empty)", i);
        }
        char icon[8]; std::snprintf(icon, sizeof(icon), "[%d]", i);
        if (select_row(icon, label)) {
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
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Shaders picker ---------------------------------------------------

void draw_shaders() {
    center_next_window(720.0f, 620.0f);
    begin_modal("Shaders");

    const auto presets = foyer::libretro::ShaderPipeline::available_presets();
    const std::string active = foyer::libretro::shader_pipeline().active();

    auto pick = [&](const std::string& name, const std::string& label) {
        const bool is_active = (name == active);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s",
            label.c_str(), is_active ? "   (active)" : "");
        if (select_row(is_active ? "[*]" : "[ ]", buf)) {
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

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
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

    center_next_window(640.0f, 620.0f);
    begin_modal("Display");

    const auto cur = foyer::libretro::VideoSinkGl::instance().aspect();
    for (const auto& r : rows) {
        const bool is_active = (r.m == cur);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s%s",
            r.label, is_active ? "   (active)" : "");
        if (select_row(is_active ? "[*]" : "[ ]", buf)) {
            foyer::libretro::VideoSinkGl::instance().set_aspect(r.m);
            g_pending = Modal::Pause;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        g_pending = Modal::Pause;
    }
    ImGui::End();
}

// ----- Core options picker --------------------------------------------

void draw_core_options() {
    center_next_window(800.0f, 620.0f);
    begin_modal("Core options");

    auto& co = foyer::libretro::CoreOptions::instance();
    const auto& opts = co.options();
    if (opts.empty()) {
        ImGui::TextDisabled("(this core publishes no options)");
    } else {
        ImGui::BeginChild("##co_scroll", ImVec2(0, 480), false);
        for (const auto& o : opts) {
            char hdr[200];
            std::snprintf(hdr, sizeof(hdr), "%s   =   %s",
                (o.desc.empty() ? o.key : o.desc).c_str(),
                o.value.c_str());
            if (ImGui::TreeNode(hdr)) {
                for (const auto& v : o.choices) {
                    const bool is_active = (v == o.value);
                    if (select_row(is_active ? "[*]" : "[ ]", v.c_str())) {
                        co.set(o.key, v);
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
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
    center_next_window(800.0f, 620.0f);
    begin_modal("Cheats");

    ensure_cheats_loaded();
    if (g_cheats_cache.empty()) {
        ImGui::TextDisabled("(no cheats loaded for this game)");
    } else {
        ImGui::BeginChild("##ch_scroll", ImVec2(0, 480), false);
        for (std::size_t i = 0; i < g_cheats_cache.size(); ++i) {
            const bool on = g_cheats_cache[i].enabled;
            if (select_row(on ? "[*]" : "[ ]",
                           g_cheats_cache[i].desc.c_str())) {
                g_cheats_cache[i].enabled = !on;
                foyer::libretro::save_cheats_for(
                    g_system_folder, g_rom_stem, g_cheats_cache);
                foyer::libretro::apply_cheats_to_core(g_cheats_cache);
            }
        }
        ImGui::EndChild();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
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
