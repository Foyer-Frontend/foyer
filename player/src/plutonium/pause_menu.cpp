#include "plutonium/pause_menu.hpp"
#include "library/config.hpp"
#include "libretro/bezel_sdl.hpp"
#include "libretro/cheats.hpp"
#include "libretro/cheevos.hpp"
#include "libretro/core_options.hpp"
#include "libretro/frontend.hpp"
#include "libretro/savestate.hpp"
#include "libretro/shader.hpp"
#include "libretro/video_sdl.hpp"
#include "platform/log.hpp"
#include "plutonium/session_tracker.hpp"

#include <switch.h>

#include <ctime>
#include <cstdio>
#include <utility>

extern "C" void retro_reset(void);

namespace foyer::player::plut {

namespace {

// Follow the foyer/HOS theme: dark UI gets a dark panel with light
// text; light UI gets a light panel with dark text. Re-polled every
// BuildMenu so flipping the system theme from the Quick Menu while
// the overlay is open is reflected on the next mode switch.
bool theme_want_dark() {
    const auto& ov = ::foyer::library::config().theme_override;
    if (ov == "light") return false;
    if (ov == "dark")  return true;
    ColorSetId id;
    if (R_FAILED(setsysGetColorSetId(&id))) return true;
    return id == ColorSetId_Dark;
}

pu::ui::Color theme_item_bg() {
    return theme_want_dark()
        ? pu::ui::Color{ 0x18, 0x18, 0x1A, 0xE6 }
        : pu::ui::Color{ 0xF4, 0xF4, 0xF6, 0xE6 };
}

pu::ui::Color theme_item_text() {
    return theme_want_dark()
        ? pu::ui::Color{ 0xF8, 0xF8, 0xFA, 0xFF }
        : pu::ui::Color{ 0x18, 0x18, 0x1A, 0xFF };
}

pu::ui::Color theme_item_focus() {
    return pu::ui::Color{ 0x3B, 0x82, 0xF6, 0xFF };
}

constexpr pu::i32       kMenuX     = 360;
constexpr pu::i32       kMenuY     = 140;
constexpr pu::i32       kMenuW     = 1200;
constexpr pu::i32       kRowH      = 80;

std::string slot_label(int i, const foyer::libretro::StateSlot& s) {
    char buf[128];
    if (s.exists) {
        std::tm tm = *std::localtime(&s.mtime);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
        std::snprintf(buf, sizeof(buf), "Slot %d   %s   %.1f KB",
            i, ts, s.size_bytes / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "Slot %d   (empty)", i);
    }
    return std::string{buf};
}

}  // namespace

PauseMenu::PauseMenu(const std::string& rp,
                     const std::string& sys,
                     const std::string& back)
    : rom_path(rp), system_folder(sys), back_nro(back) {
    rom_stem = rp;
    if (const auto sl = rom_stem.find_last_of('/'); sl != std::string::npos)
        rom_stem = rom_stem.substr(sl + 1);
    if (const auto dot = rom_stem.find_last_of('.'); dot != std::string::npos)
        rom_stem = rom_stem.substr(0, dot);
}

bool PauseMenu::OnBack() {
    switch (mode) {
        case PauseMode::Pause:
            return false;   // close pause overlay
        case PauseMode::DisplayAspect:
        case PauseMode::DisplayBezel:
            ChangeMode(PauseMode::Display);  // sub -> Display root
            return true;
        default:
            ChangeMode(PauseMode::Pause);    // sub -> Pause root
            return true;
    }
}

void PauseMenu::ChangeMode(PauseMode m) {
    // Switching to a different mode resets the row highlight; staying
    // within the same mode (cheat toggle, core-option cycle, bezel
    // flip — all of which set last_selected_idx in the callback BEFORE
    // firing on_mode_changed) keeps it where it was.
    if (m != mode) last_selected_idx = 0;
    mode = m;
    if (on_mode_changed) on_mode_changed();
}

pu::ui::elm::Menu::Ref PauseMenu::BuildMenu() {
    // Build a fresh Menu element and populate it for the current
    // mode. The submenus (DisplayAspect / DisplayBezel) don't have
    // Build* helpers — they fall through to Populate* directly via
    // this generic path.
    auto m = pu::ui::elm::Menu::New(MenuX, MenuY, MenuW,
        theme_item_bg(),
        theme_item_focus(),
        RowH, RowsToShow);
    Populate(m);
    // Restore the row the user last activated so on_mode_changed
    // rebuilds (cheat toggle, shader pick, core-option cycle) don't
    // dump the highlight back to row 0. Plutonium clamps internally,
    // so it's safe to set this even when the new mode has fewer
    // items than the previous one.
    m->SetSelectedIndex((u32)std::max(0, last_selected_idx));
    return m;
}

void PauseMenu::Populate(pu::ui::elm::Menu::Ref& menu) {
    menu->ClearItems();
    switch (mode) {
        case PauseMode::SaveSlot:      PopulateSlotPicker(menu, true);   break;
        case PauseMode::LoadSlot:      PopulateSlotPicker(menu, false);  break;
        case PauseMode::Shaders:       PopulateShaders(menu);            break;
        case PauseMode::Display:       PopulateDisplay(menu);            break;
        case PauseMode::DisplayAspect: PopulateDisplayAspect(menu);      break;
        case PauseMode::DisplayBezel:  PopulateDisplayBezel(menu);       break;
        case PauseMode::CoreOptions:   PopulateCoreOptions(menu);        break;
        case PauseMode::Cheats:        PopulateCheats(menu);             break;
        case PauseMode::Achievements:  PopulateAchievements(menu);       break;
        case PauseMode::Pause:
        default:                       PopulatePauseRoot(menu);          break;
    }
    menu->ForceReloadItems();
}

// ---- Populate* implementations -----------------------------------
// Identical item-construction logic to the Build*() variants above;
// they just AddItem against the supplied shell instead of creating
// a fresh Menu.

void PauseMenu::PopulatePauseRoot(pu::ui::elm::Menu::Ref& menu) {
    int row = 0;
    auto add = [&](const std::string& label,
                   pu::ui::elm::MenuItem::OnKeyCallback cb) {
        const int my_row = row++;
        auto item = pu::ui::elm::MenuItem::New(label);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, my_row, cb = std::move(cb)]() {
            last_selected_idx = my_row;
            if (cb) cb();
        });
        menu->AddItem(item);
    };
    add("Resume", [this]() { if (on_close) on_close(); });
    add("Restart rom", [this]() {
        ::retro_reset();
        if (on_close) on_close();
    });
    add("Save state",    [this]() { ChangeMode(PauseMode::SaveSlot); });
    add("Load state",    [this]() { ChangeMode(PauseMode::LoadSlot); });
    add("Core options",  [this]() { ChangeMode(PauseMode::CoreOptions); });
    add("Display options", [this]() { ChangeMode(PauseMode::Display); });
    add("Shaders",       [this]() { ChangeMode(PauseMode::Shaders); });
    add("Cheats",        [this]() { ChangeMode(PauseMode::Cheats); });
    // Only surface Achievements when rcheevos identified a set for
    // the current rom — otherwise the row would open an empty list
    // and confuse the user about whether their RA creds are wired
    // up. Label includes the running unlocked/total counter so the
    // user can see progress without entering the picker.
    {
        const auto& ch = foyer::libretro::Cheevos::instance();
        if (ch.active() && ch.total_count() > 0) {
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl),
                "Achievements (%d/%d)",
                ch.unlocked_count(), ch.total_count());
            add(lbl, [this]() { ChangeMode(PauseMode::Achievements); });
        }
    }
    add("Quit to foyer", [this]() {
        // Persist the session BEFORE envSetNextLoad fires so
        // per_game.jsonc is fully written by the time HOS
        // chain-launches the browser back. finalize() is
        // idempotent — the second Quit cell at the bottom of
        // PopulatePauseRoot won't double-count if both fire.
        SessionTracker::instance().finalize();
        foyer::libretro::Frontend::instance().flush_sram();
        if (!back_nro.empty()) {
            const std::string sd = "sdmc:" + back_nro;
            char a[512]; std::snprintf(a, sizeof(a), "\"%s\"", sd.c_str());
            envSetNextLoad(sd.c_str(), a);
        }
        if (on_quit) on_quit();
    });
}

void PauseMenu::PopulateSlotPicker(pu::ui::elm::Menu::Ref& menu, bool save_mode) {
    foyer::libretro::StateSlot slots[foyer::libretro::kStateSlotCount];
    foyer::libretro::inspect_slots(rom_path, system_folder, slots);
    for (int i = 0; i < foyer::libretro::kStateSlotCount; ++i) {
        const int idx = i;
        const bool exists = slots[i].exists;
        auto item = pu::ui::elm::MenuItem::New(slot_label(i, slots[i]));
        item->SetColor(theme_item_text());
        item->AddOnKey([this, idx, save_mode, exists]() {
            last_selected_idx = idx;
            const auto path = foyer::libretro::state_path_for(
                rom_path, system_folder, idx);
            if (save_mode) {
                foyer::libretro::save_state(path);
                ChangeMode(PauseMode::Pause);
            } else if (exists) {
                if (foyer::libretro::load_state(path)) {
                    if (on_close) on_close();
                }
            }
        });
        menu->AddItem(item);
    }
}

void PauseMenu::PopulateShaders(pu::ui::elm::Menu::Ref& menu) {
    const auto presets = foyer::libretro::ShaderPipeline::available_presets();
    const std::string active = foyer::libretro::shader_pipeline().active();
    int row = 0;
    auto add = [&](const std::string& name, const std::string& label) {
        const int my_row = row++;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s",
            label.c_str(), (name == active) ? "   (active)" : "");
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, my_row, name]() {
            last_selected_idx = my_row;
            foyer::libretro::shader_pipeline().set_preset(name);
            foyer::library::set_shader_name(name);
            // Live preview against the frozen pause frame so the
            // user sees the new shader applied without resuming.
            foyer::libretro::VideoSinkSdl::instance().reprocess_shader();
            // Stay on the shader picker so the "(active)" tag
            // updates and the user can audition more presets.
            if (on_mode_changed) on_mode_changed();
        });
        menu->AddItem(item);
    };
    add("none", "None");
    for (const auto& p : presets) {
        if (p.name == "none") continue;
        add(p.name, p.label.empty() ? p.name : p.label);
    }
}

namespace {
const char* aspect_label(foyer::libretro::AspectMode m) {
    using foyer::libretro::AspectMode;
    switch (m) {
        case AspectMode::DisplayCore: return "Core default";
        case AspectMode::Display43:   return "4:3";
        case AspectMode::Display169:  return "16:9";
        case AspectMode::Stretch:     return "Stretch";
        case AspectMode::Integer1x:   return "Integer 1x";
        case AspectMode::Integer2x:   return "Integer 2x";
        case AspectMode::IntegerAuto: return "Integer auto";
    }
    return "?";
}
}

// Display root: routes to the aspect submenu, toggles the bezel,
// routes to the bezel picker. The actual aspect / bezel choice
// lives one level deeper.
void PauseMenu::PopulateDisplay(pu::ui::elm::Menu::Ref& menu) {
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Aspect   %s",
            aspect_label(foyer::libretro::VideoSinkSdl::instance().aspect()));
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this]() {
            last_selected_idx = 0;
            ChangeMode(PauseMode::DisplayAspect);
        });
        menu->AddItem(item);
    }
    {
        const bool on = foyer::library::config().show_bezels;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Bezel   %s",
            on ? "(ON)" : "(OFF)");
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, on]() {
            last_selected_idx = 1;
            foyer::library::set_bool("show_bezels", !on);
            foyer::libretro::bezel_sdl_invalidate();
            if (on_mode_changed) on_mode_changed();
        });
        menu->AddItem(item);
    }
    {
        auto item = pu::ui::elm::MenuItem::New("Pick bezel");
        item->SetColor(theme_item_text());
        item->AddOnKey([this]() {
            last_selected_idx = 2;
            ChangeMode(PauseMode::DisplayBezel);
        });
        menu->AddItem(item);
    }
}

void PauseMenu::PopulateDisplayAspect(pu::ui::elm::Menu::Ref& menu) {
    using foyer::libretro::AspectMode;
    static const AspectMode kAspects[] = {
        AspectMode::DisplayCore, AspectMode::Display43,
        AspectMode::Display169,  AspectMode::Stretch,
        AspectMode::Integer1x,   AspectMode::Integer2x,
        AspectMode::IntegerAuto,
    };
    const auto cur = foyer::libretro::VideoSinkSdl::instance().aspect();
    int row = 0;
    for (auto m : kAspects) {
        const int my_row = row++;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s",
            aspect_label(m), (m == cur) ? "   (active)" : "");
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, my_row, m]() {
            last_selected_idx = my_row;
            foyer::libretro::VideoSinkSdl::instance().set_aspect(m);
            ChangeMode(PauseMode::Display);
        });
        menu->AddItem(item);
    }
}

void PauseMenu::PopulateDisplayBezel(pu::ui::elm::Menu::Ref& menu) {
    // Phase P4 — list any per-rom bundle bezel that we can find
    // under /foyer/assets/system/<sys>/<stem>/, plus the per-system
    // fallback. Future work: also enumerate /foyer/content/bezels/
    // entries the user dropped in. For now we expose enough to flip
    // between "bundle" art (which the scraper picks per region) and
    // the per-system default.
    auto add = [&](const std::string& label, const std::string& /*path*/) {
        auto item = pu::ui::elm::MenuItem::New(label);
        item->SetColor(theme_item_text());
        item->AddOnKey([this]() {
            // Phase P4.5: hook a per-game bezel override config so
            // the resolve_path() chain can prefer the user's pick.
            // For now just return to the Display root.
            ChangeMode(PauseMode::Display);
        });
        menu->AddItem(item);
    };
    add("Auto (per-rom -> per-system)", "");
    add("(no other bezels found)", "");
}

void PauseMenu::PopulateCoreOptions(pu::ui::elm::Menu::Ref& menu) {
    auto& co = foyer::libretro::CoreOptions::instance();
    const auto& opts = co.options();
    if (opts.empty()) {
        auto item = pu::ui::elm::MenuItem::New("(no core options)");
        item->SetColor({ 0xA0, 0xA0, 0xA8, 0xFF });
        menu->AddItem(item);
        return;
    }
    int co_row = 0;
    for (const auto& o : opts) {
        char hdr[200];
        std::snprintf(hdr, sizeof(hdr), "%s   =   %s",
            (o.desc.empty() ? o.key : o.desc).c_str(),
            o.value.c_str());
        const std::string key = o.key;
        const auto choices = o.choices;
        const std::string current = o.value;
        auto item = pu::ui::elm::MenuItem::New(hdr);
        item->SetColor(theme_item_text());
        const int my_row = co_row++;
        item->AddOnKey([this, my_row, key, choices, current]() {
            last_selected_idx = my_row;
            auto& co_ref = foyer::libretro::CoreOptions::instance();
            std::size_t idx = 0;
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (choices[i] == current) { idx = i; break; }
            }
            const std::size_t next = (idx + 1) % choices.size();
            co_ref.set(key, choices[next]);
            if (on_mode_changed) on_mode_changed();
        });
        menu->AddItem(item);
    }
}

void PauseMenu::PopulateAchievements(pu::ui::elm::Menu::Ref& menu) {
    const auto rows = foyer::libretro::Cheevos::instance().list();
    if (rows.empty()) {
        auto item = pu::ui::elm::MenuItem::New(
            "(no achievements identified for this rom)");
        item->SetColor({ 0xA0, 0xA0, 0xA8, 0xFF });
        menu->AddItem(item);
        return;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        char buf[256];
        // Leading glyph: filled circle when unlocked, hollow when
        // still locked. Plutonium's font ships UTF-8 so the bullets
        // render natively; no special font setup needed.
        const char* mark = r.unlocked ? "\xE2\x97\x8F" /* ● */
                                       : "\xE2\x97\x8B" /* ○ */;
        std::snprintf(buf, sizeof(buf), "%s  %s   (%d pts)",
            mark, r.title.c_str(), r.points);
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(r.unlocked ? theme_item_text()
                                  : pu::ui::Color{ 0xA0, 0xA0, 0xA8, 0xFF });
        const std::size_t idx = i;
        item->AddOnKey([this, idx]() { last_selected_idx = (int)idx; });
        menu->AddItem(item);
    }
}

void PauseMenu::PopulateCheats(pu::ui::elm::Menu::Ref& menu) {
    auto cheats = foyer::libretro::load_cheats_for(system_folder, rom_stem);
    if (cheats.empty()) {
        auto item = pu::ui::elm::MenuItem::New("(no cheats for this game)");
        item->SetColor({ 0xA0, 0xA0, 0xA8, 0xFF });
        menu->AddItem(item);
        return;
    }
    auto cheat_state = std::make_shared<std::vector<foyer::libretro::Cheat>>(
        std::move(cheats));
    const auto sys = system_folder;
    const auto stem = rom_stem;
    for (std::size_t i = 0; i < cheat_state->size(); ++i) {
        const std::size_t idx = i;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s %s",
            (*cheat_state)[i].enabled ? "[ON] " : "[  ] ",
            (*cheat_state)[i].desc.c_str());
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, cheat_state, sys, stem, idx]() {
            last_selected_idx = (int)idx;
            (*cheat_state)[idx].enabled = !(*cheat_state)[idx].enabled;
            foyer::libretro::save_cheats_for(sys, stem, *cheat_state);
            foyer::libretro::apply_cheats_to_core(*cheat_state);
            if (on_mode_changed) on_mode_changed();
        });
        menu->AddItem(item);
    }
}

pu::ui::elm::Menu::Ref PauseMenu::BuildPauseRoot() {
    auto m = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 9);

    auto add = [&](const std::string& label,
                   pu::ui::elm::MenuItem::OnKeyCallback cb) {
        auto item = pu::ui::elm::MenuItem::New(label);
        item->SetColor(theme_item_text());
        item->AddOnKey(std::move(cb));
        m->AddItem(item);
    };

    add("Resume", [this]() {
        if (on_close) on_close();
    });
    add("Restart rom", [this]() {
        ::retro_reset();
        if (on_close) on_close();
    });
    add("Save state", [this]() {
        ChangeMode(PauseMode::SaveSlot);
    });
    add("Load state", [this]() {
        ChangeMode(PauseMode::LoadSlot);
    });

    add("Core options", [this]() {
        ChangeMode(PauseMode::CoreOptions);
    });
    add("Display — aspect", [this]() {
        ChangeMode(PauseMode::Display);
    });
    add("Shaders", [this]() {
        ChangeMode(PauseMode::Shaders);
    });
    add("Cheats", [this]() {
        ChangeMode(PauseMode::Cheats);
    });
    add("Quit to foyer", [this]() {
        SessionTracker::instance().finalize();
        foyer::libretro::Frontend::instance().flush_sram();
        if (!back_nro.empty()) {
            const std::string sd = "sdmc:" + back_nro;
            char a[512]; std::snprintf(a, sizeof(a), "\"%s\"", sd.c_str());
            envSetNextLoad(sd.c_str(), a);
        }
        if (on_quit) on_quit();
    });
    return m;
}

pu::ui::elm::Menu::Ref PauseMenu::BuildSlotPicker(bool save_mode) {
    auto m = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 10);

    foyer::libretro::StateSlot slots[foyer::libretro::kStateSlotCount];
    foyer::libretro::inspect_slots(rom_path, system_folder, slots);

    for (int i = 0; i < foyer::libretro::kStateSlotCount; ++i) {
        const int idx = i;
        const bool exists = slots[i].exists;
        auto item = pu::ui::elm::MenuItem::New(slot_label(i, slots[i]));
        item->SetColor(theme_item_text());
        item->AddOnKey([this, idx, save_mode, exists]() {
            last_selected_idx = idx;
            const auto path = foyer::libretro::state_path_for(
                rom_path, system_folder, idx);
            if (save_mode) {
                if (foyer::libretro::save_state(path)) {
                    foyer::log::write("[pause] saved slot %d -> %s\n",
                        idx, path.c_str());
                }
                ChangeMode(PauseMode::Pause);
            } else if (exists) {
                if (foyer::libretro::load_state(path)) {
                    foyer::log::write("[pause] loaded slot %d <- %s\n",
                        idx, path.c_str());
                    if (on_close) on_close();
                }
            }
        });
        m->AddItem(item);
    }
    return m;
}

pu::ui::elm::Menu::Ref PauseMenu::BuildShaders() {
    auto m = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 9);

    const auto presets = foyer::libretro::ShaderPipeline::available_presets();
    const std::string active = foyer::libretro::shader_pipeline().active();

    auto add = [&](const std::string& name, const std::string& label) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s",
            label.c_str(), (name == active) ? "   (active)" : "");
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, name]() {
            foyer::libretro::shader_pipeline().set_preset(name);
            foyer::library::set_shader_name(name);
            ChangeMode(PauseMode::Pause);
        });
        m->AddItem(item);
    };
    add("none", "None");
    for (const auto& p : presets) {
        if (p.name == "none") continue;
        add(p.name, p.label.empty() ? p.name : p.label);
    }
    return m;
}

pu::ui::elm::Menu::Ref PauseMenu::BuildDisplay() {
    using foyer::libretro::AspectMode;
    struct Row { const char* label; AspectMode m; };
    static const Row rows[] = {
        { "Core default",   AspectMode::DisplayCore  },
        { "4:3",            AspectMode::Display43    },
        { "16:9",           AspectMode::Display169   },
        { "Stretch",        AspectMode::Stretch      },
        { "Integer 1x",     AspectMode::Integer1x    },
        { "Integer 2x",     AspectMode::Integer2x    },
        { "Integer auto",   AspectMode::IntegerAuto  },
    };
    auto menu = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 8);
    const auto cur = foyer::libretro::VideoSinkSdl::instance().aspect();
    for (const auto& r : rows) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s",
            r.label, (r.m == cur) ? "   (active)" : "");
        const auto mode_to_set = r.m;
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, mode_to_set]() {
            foyer::libretro::VideoSinkSdl::instance().set_aspect(mode_to_set);
            ChangeMode(PauseMode::Pause);
        });
        menu->AddItem(item);
    }
    return menu;
}

pu::ui::elm::Menu::Ref PauseMenu::BuildCoreOptions() {
    auto menu = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 8);
    auto& co = foyer::libretro::CoreOptions::instance();
    const auto& opts = co.options();
    if (opts.empty()) {
        auto item = pu::ui::elm::MenuItem::New("(no core options)");
        item->SetColor({ 0xA0, 0xA0, 0xA8, 0xFF });
        menu->AddItem(item);
    } else {
        for (const auto& o : opts) {
            char hdr[200];
            std::snprintf(hdr, sizeof(hdr), "%s   =   %s",
                (o.desc.empty() ? o.key : o.desc).c_str(),
                o.value.c_str());
            auto item = pu::ui::elm::MenuItem::New(hdr);
            item->SetColor(theme_item_text());
            // Cycle to next choice on A — quick UX without nested
            // pickers; Phase P5 may swap for a dedicated value
            // picker layout.
            const std::string key = o.key;
            const auto choices = o.choices;
            const std::string current = o.value;
            item->AddOnKey([key, choices, current]() {
                auto& co_ref = foyer::libretro::CoreOptions::instance();
                std::size_t idx = 0;
                for (std::size_t i = 0; i < choices.size(); ++i) {
                    if (choices[i] == current) { idx = i; break; }
                }
                const std::size_t next = (idx + 1) % choices.size();
                co_ref.set(key, choices[next]);
            });
            menu->AddItem(item);
        }
    }
    return menu;
}

pu::ui::elm::Menu::Ref PauseMenu::BuildCheats() {
    auto menu = pu::ui::elm::Menu::New(kMenuX, kMenuY, kMenuW,
        theme_item_bg(), theme_item_focus(), kRowH, 8);
    auto cheats = foyer::libretro::load_cheats_for(system_folder, rom_stem);
    if (cheats.empty()) {
        auto item = pu::ui::elm::MenuItem::New("(no cheats for this game)");
        item->SetColor({ 0xA0, 0xA0, 0xA8, 0xFF });
        menu->AddItem(item);
        return menu;
    }
    // Make a heap copy so the toggle callback survives.
    auto cheat_state = std::make_shared<std::vector<foyer::libretro::Cheat>>(
        std::move(cheats));
    const auto sys = system_folder;
    const auto stem = rom_stem;
    for (std::size_t i = 0; i < cheat_state->size(); ++i) {
        const std::size_t idx = i;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s %s",
            (*cheat_state)[i].enabled ? "[ON] " : "[  ] ",
            (*cheat_state)[i].desc.c_str());
        auto item = pu::ui::elm::MenuItem::New(buf);
        item->SetColor(theme_item_text());
        item->AddOnKey([this, cheat_state, sys, stem, idx]() {
            last_selected_idx = (int)idx;
            (*cheat_state)[idx].enabled = !(*cheat_state)[idx].enabled;
            foyer::libretro::save_cheats_for(sys, stem, *cheat_state);
            foyer::libretro::apply_cheats_to_core(*cheat_state);
            if (on_mode_changed) on_mode_changed();
        });
        menu->AddItem(item);
    }
    return menu;
}

}  // namespace foyer::player::plut
