#include "overlay.hpp"
#include "core_options.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace foyer::libretro {
namespace {

// L3 + R3 — pressing both sticks at once. Minus/Plus collide with libretro
// cores that use them for in-game menu / pause, so we moved to a combo no
// emulated console actually exposes.
constexpr std::uint64_t kCombo = HidNpadButton_StickL | HidNpadButton_StickR;

constexpr const char* kMainItems[] = {
    "Save State",
    "Load State",
    "Settings",
    "Core Options",
    "Cheats",
    "Quit Game",
};
constexpr int kMainCount = (int)(sizeof(kMainItems) / sizeof(kMainItems[0]));

enum : int {
    MainSave    = 0,
    MainLoad    = 1,
    MainSettings = 2,
    MainCoreOpts = 3,
    MainCheats   = 4,
    MainQuit     = 5,
};

constexpr const char* kAspectLabels[] = {
    "Display 4:3",
    "Display 16:9",
    "Display (Core)",
    "Stretch",
    "Integer 1x",
    "Integer 2x",
    "Integer Auto",
};
constexpr AspectMode kAspectValues[] = {
    AspectMode::Display43,
    AspectMode::Display169,
    AspectMode::DisplayCore,
    AspectMode::Stretch,
    AspectMode::Integer1x,
    AspectMode::Integer2x,
    AspectMode::IntegerAuto,
};
constexpr int kAspectCount = (int)(sizeof(kAspectValues) / sizeof(kAspectValues[0]));

constexpr NVGcolor kAccent      = { { { 0xF6/255.f, 0xC1/255.f, 0x42/255.f, 1.0f } } };
constexpr NVGcolor kPanel       = { { { 0x18/255.f, 0x1B/255.f, 0x21/255.f, 0.96f } } };
constexpr NVGcolor kRow         = { { { 0x21/255.f, 0x25/255.f, 0x2D/255.f, 1.0f } } };
constexpr NVGcolor kBg          = { { { 0x10/255.f, 0x12/255.f, 0x16/255.f, 1.0f } } };
constexpr NVGcolor kTextStrong  = { { { 0xF2/255.f, 0xF2/255.f, 0xF2/255.f, 1.0f } } };
constexpr NVGcolor kText        = { { { 0xCB/255.f, 0xCD/255.f, 0xD2/255.f, 1.0f } } };
constexpr NVGcolor kTextDim     = { { { 0x82/255.f, 0x86/255.f, 0x8E/255.f, 1.0f } } };
constexpr NVGcolor kBorder      = { { { 0x2D/255.f, 0x32/255.f, 0x3A/255.f, 1.0f } } };

void rrect(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void rrect_outline(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c, float thick) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, thick);
    nvgStroke(vg);
}

void format_mtime(std::time_t t, char* out, std::size_t cap) {
    if (t == 0) {
        std::snprintf(out, cap, "—");
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    std::snprintf(out, cap, "%04d-%02d-%02d %02d:%02d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min);
}

} // namespace

void Overlay::refresh_slots() {
    if (m_hooks.probe_slots) m_hooks.probe_slots(m_slots);
    m_slots_dirty = false;
}

Overlay::Action Overlay::update(std::uint64_t held, std::uint64_t down,
                                const OverlayTouch& touch,
                                float screen_w, float screen_h) {
    if (m_toast_ttl > 0) m_toast_ttl--;

    const bool combo_held = (held & kCombo) == kCombo;
    const bool combo_press = combo_held && !m_combo_was_held;
    m_combo_was_held = combo_held;

    if (combo_press) {
        if (m_state == State::Hidden) {
            m_state = State::Main;
            m_main_index = 0;
        } else {
            m_state = State::Hidden;
        }
        return Action::None;
    }

    if (m_state == State::Hidden) return Action::None;

    // Touch hit-testing. Layout MUST mirror draw() — if you change
    // pw/ph/list_y/row_h there, mirror them here. Tap on a row that's
    // already focused acts (synthesises A); tap on a different row
    // moves focus first. Tap on the dim outside dismisses.
    if (touch.tap_started && screen_w > 0.0f && screen_h > 0.0f) {
        const float pw = 560.0f;
        const float ph = 460.0f;
        const float px = (screen_w - pw) * 0.5f;
        const float py = (screen_h - ph) * 0.5f;
        const float list_x = px + 24;
        const float list_y = py + 84;
        const float list_w = pw - 48;
        const float row_h  = 38.0f;

        const bool inside_panel =
            touch.x >= px && touch.x < px + pw &&
            touch.y >= py && touch.y < py + ph;
        if (!inside_panel) {
            // Tap outside the panel = dismiss to Main / Hidden.
            if (m_state == State::Main) m_state = State::Hidden;
            else                        m_state = State::Main;
            return Action::None;
        }
        if (touch.x >= list_x && touch.x < list_x + list_w &&
            touch.y >= list_y) {
            const int row_idx = (int)((touch.y - list_y) / row_h);
            if (row_idx >= 0) {
                int* focus = nullptr;
                int  count = 0;
                switch (m_state) {
                    case State::Main:        focus = &m_main_index;     count = kMainCount;     break;
                    case State::SaveSlots:
                    case State::LoadSlots:   focus = &m_slot_index;     count = kStateSlotCount; break;
                    case State::Settings:    focus = &m_settings_index; count = kAspectCount;   break;
                    case State::CoreOptions: focus = &m_core_opt_index; count = 0; break; // dynamic; A still works after focus
                    case State::Cheats:      focus = &m_cheat_index;    count = (int)m_cheats.size(); break;
                    default: break;
                }
                if (focus && row_idx < count) {
                    if (*focus == row_idx) {
                        // Re-tap on focused row -> synthesise A.
                        down |= HidNpadButton_A;
                    } else {
                        *focus = row_idx;
                    }
                }
            }
        }
    }

    auto pressed = [&](std::uint64_t b) { return (down & b) != 0; };

    auto nav_up   = pressed(HidNpadButton_Up   | HidNpadButton_StickLUp);
    auto nav_down = pressed(HidNpadButton_Down | HidNpadButton_StickLDown);

    if (m_state == State::Main) {
        if (nav_up)   m_main_index = (m_main_index - 1 + kMainCount) % kMainCount;
        if (nav_down) m_main_index = (m_main_index + 1) % kMainCount;
        if (pressed(HidNpadButton_B)) {
            m_state = State::Hidden;
            return Action::None;
        }
        if (pressed(HidNpadButton_A)) {
            switch (m_main_index) {
                case MainSave:
                    m_state = State::SaveSlots;
                    m_slot_index = 0;
                    refresh_slots();
                    break;
                case MainLoad:
                    m_state = State::LoadSlots;
                    m_slot_index = 0;
                    refresh_slots();
                    break;
                case MainSettings:
                    m_state = State::Settings;
                    m_settings_index = 0;
                    break;
                case MainCoreOpts:
                    m_state = State::CoreOptions;
                    m_core_opt_index  = 0;
                    m_core_opt_scroll = 0;
                    break;
                case MainCheats:
                    m_state = State::Cheats;
                    m_cheat_index = 0;
                    if (m_cheats.empty()) {
                        m_cheats = load_cheats_for(m_rom_folder, m_rom_stem);
                        // Push the persisted enable state to the core
                        // so toggles applied last session take effect
                        // before the user opens the menu.
                        if (!m_cheats.empty()) apply_cheats_to_core(m_cheats);
                    }
                    break;
                case MainQuit:
                    m_state = State::Hidden;
                    return Action::Quit;
            }
        }
    } else if (m_state == State::Settings) {
        if (nav_up)   m_settings_index = (m_settings_index - 1 + kAspectCount) % kAspectCount;
        if (nav_down) m_settings_index = (m_settings_index + 1) % kAspectCount;
        if (pressed(HidNpadButton_B)) m_state = State::Main;
        else if (pressed(HidNpadButton_A) && m_hooks.set_aspect) {
            m_hooks.set_aspect(kAspectValues[m_settings_index]);
        }
    } else if (m_state == State::CoreOptions) {
        const auto& opts = CoreOptions::instance().options();
        const int n = (int)opts.size();
        if (n == 0) {
            if (pressed(HidNpadButton_B)) m_state = State::Main;
        } else {
            if (nav_up)   m_core_opt_index = (m_core_opt_index - 1 + n) % n;
            if (nav_down) m_core_opt_index = (m_core_opt_index + 1) % n;
            if (pressed(HidNpadButton_B)) m_state = State::Main;
            else {
                const auto& cur = opts[m_core_opt_index];
                const int cn = (int)cur.choices.size();
                const bool right = pressed(HidNpadButton_Right);
                const bool left  = pressed(HidNpadButton_Left);
                if (cn > 1 && (right || left)) {
                    int ci = 0;
                    for (int i = 0; i < cn; i++)
                        if (cur.choices[i] == cur.value) { ci = i; break; }
                    ci = right ? (ci + 1) % cn : (ci - 1 + cn) % cn;
                    CoreOptions::instance().set(cur.key, cur.choices[ci]);
                }
            }
        }
    } else if (m_state == State::SaveSlots || m_state == State::LoadSlots) {
        if (nav_up)   m_slot_index = (m_slot_index - 1 + kStateSlotCount) % kStateSlotCount;
        if (nav_down) m_slot_index = (m_slot_index + 1) % kStateSlotCount;
        if (pressed(HidNpadButton_B)) m_state = State::Main;
        else if (pressed(HidNpadButton_A)) {
            m_result_slot = m_slot_index;
            const auto saving = (m_state == State::SaveSlots);
            m_state = State::Hidden;
            return saving ? Action::SaveStateSlot : Action::LoadStateSlot;
        }
    } else if (m_state == State::Cheats) {
        const int n = (int)m_cheats.size();
        if (n == 0) {
            if (pressed(HidNpadButton_B)) m_state = State::Main;
        } else {
            if (nav_up)   m_cheat_index = (m_cheat_index - 1 + n) % n;
            if (nav_down) m_cheat_index = (m_cheat_index + 1) % n;
            if (pressed(HidNpadButton_B)) m_state = State::Main;
            else if (pressed(HidNpadButton_A)) {
                // Toggle the focused cheat. Push the new enable list
                // to the core immediately AND save back to the .cht so
                // the toggle survives the next launch.
                m_cheats[m_cheat_index].enabled =
                    !m_cheats[m_cheat_index].enabled;
                apply_cheats_to_core(m_cheats);
                save_cheats_for(m_rom_folder, m_rom_stem, m_cheats);
            }
        }
    }
    return Action::None;
}

void Overlay::draw_panel(NVGcontext* vg, float w, float h, const char* title) {
    const float pw = 560.0f;
    const float ph = 460.0f;
    const float px = (w - pw) * 0.5f;
    const float py = (h - ph) * 0.5f;

    // Drop shadow behind panel.
    auto shadow = nvgBoxGradient(vg, px + 4, py + 6, pw, ph, 18.0f, 24.0f,
        nvgRGBAf(0, 0, 0, 0.55f), nvgRGBAf(0, 0, 0, 0.0f));
    nvgBeginPath(vg);
    nvgRect(vg, px - 30, py - 30, pw + 60, ph + 60);
    nvgRoundedRect(vg, px, py, pw, ph, 18.0f);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);

    rrect(vg, px, py, pw, ph, 18.0f, kPanel);
    rrect_outline(vg, px, py, pw, ph, 18.0f, kBorder, 1.0f);

    nvgFontSize(vg, 30.0f);
    nvgFillColor(vg, kTextStrong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, px + 28, py + 36, title, nullptr);

    // Underline accent.
    rrect(vg, px + 28, py + 60, 56, 3, 1.5f, kAccent);

    // Footer hint bar.
    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, kTextDim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    using namespace foyer::ui::icons;
    std::string hint;
    switch (m_state) {
        case State::Main:
            hint = std::string{A} + " select   " + B + " close   "
                 + Minus + " " + Plus + " hide overlay";
            break;
        case State::SaveSlots:
            hint = std::string{A} + " save to slot   " + B + " back";
            break;
        case State::LoadSlots:
            hint = std::string{A} + " load from slot   " + B + " back";
            break;
        case State::Settings:
            hint = std::string{A} + " apply   " + B + " back";
            break;
        case State::CoreOptions:
            hint = std::string{Left} + Right + " change   " + B + " back";
            break;
        case State::Cheats:
            hint = std::string{A} + " toggle   " + B + " back";
            break;
        default: break;
    }
    if (!hint.empty()) nvgText(vg, w * 0.5f, py + ph - 16, hint.c_str(), nullptr);
}

void Overlay::draw(NVGcontext* vg, float w, float h) {
    if (m_state == State::Hidden && m_toast_ttl <= 0) return;

    if (m_toast_ttl > 0) {
        const float tw = 360.0f;
        const float th = 56.0f;
        const float tx = (w - tw) * 0.5f;
        const float ty = h - th - 24.0f;
        const float a  = std::min(1.0f, (float)m_toast_ttl / 30.0f);
        rrect(vg, tx, ty, tw, th, 12.0f, nvgRGBAf(0.05f, 0.06f, 0.08f, 0.85f * a));
        rrect_outline(vg, tx, ty, tw, th, 12.0f, nvgRGBAf(1, 1, 1, 0.10f * a), 1.0f);
        nvgFontSize(vg, 22.0f);
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, a));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, w * 0.5f, ty + th * 0.5f, m_toast.c_str(), nullptr);
    }

    if (m_state == State::Hidden) return;

    // Dim underlying game.
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
    nvgFill(vg);

    const char* title = nullptr;
    switch (m_state) {
        case State::Main:        title = "Pause"; break;
        case State::SaveSlots:   title = "Save State"; break;
        case State::LoadSlots:   title = "Load State"; break;
        case State::Settings:    title = "Settings"; break;
        case State::CoreOptions: title = "Core Options"; break;
        case State::Cheats:      title = "Cheats"; break;
        default: title = "";
    }
    draw_panel(vg, w, h, title);

    const float pw = 560.0f;
    const float ph = 460.0f;
    const float px = (w - pw) * 0.5f;
    const float py = (h - ph) * 0.5f;
    const float list_x = px + 24;
    const float list_y = py + 84;
    const float list_w = pw - 48;
    const float row_h  = 38.0f;

    auto row = [&](int i, bool sel, const char* label, const char* rhs, bool dim_row) {
        const float ry = list_y + i * row_h;
        rrect(vg, list_x, ry, list_w, row_h - 4, 8.0f,
              sel ? kAccent : kRow);

        nvgFontSize(vg, 20.0f);
        nvgFillColor(vg, sel ? kBg : (dim_row ? kTextDim : kText));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, list_x + 16, ry + (row_h - 4) * 0.5f, label, nullptr);

        if (rhs && rhs[0]) {
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, sel ? kBg : kTextDim);
            nvgText(vg, list_x + list_w - 16, ry + (row_h - 4) * 0.5f, rhs, nullptr);
        }
    };

    if (m_state == State::Main) {
        for (int i = 0; i < kMainCount; i++) {
            row(i, i == m_main_index, kMainItems[i], "", false);
        }
    } else if (m_state == State::Settings) {
        const auto current = m_hooks.get_aspect ? m_hooks.get_aspect()
                                                : AspectMode::DisplayCore;
        for (int i = 0; i < kAspectCount; i++) {
            const bool active = (kAspectValues[i] == current);
            row(i, i == m_settings_index, kAspectLabels[i], active ? "active" : "", false);
        }
    } else if (m_state == State::CoreOptions) {
        const auto& opts = CoreOptions::instance().options();
        if (opts.empty()) {
            nvgFontSize(vg, 18.0f);
            nvgFillColor(vg, kTextDim);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, list_x + list_w * 0.5f, list_y + 64,
                    "this core has no exposed options", nullptr);
        } else {
            const int visible = (int)((ph - 84 - 28) / row_h);
            int first = m_core_opt_scroll;
            if (m_core_opt_index < first)                 first = m_core_opt_index;
            if (m_core_opt_index >= first + visible)      first = m_core_opt_index - visible + 1;
            if (first < 0)                                first = 0;
            m_core_opt_scroll = first;

            for (int i = 0; i < visible && first + i < (int)opts.size(); i++) {
                const auto& o = opts[first + i];
                const bool sel = (first + i == m_core_opt_index);
                std::string rhs = "< " + o.value + " >";
                row(i, sel,
                    o.desc.empty() ? o.key.c_str() : o.desc.c_str(),
                    rhs.c_str(),
                    false);
            }
        }
    } else if (m_state == State::SaveSlots || m_state == State::LoadSlots) {
        for (int i = 0; i < kStateSlotCount; i++) {
            char label[32];
            if (i == 0)  std::snprintf(label, sizeof(label), "Quick");
            else         std::snprintf(label, sizeof(label), "Slot %d", i);

            char ts[64];
            if (m_slots[i].exists) {
                format_mtime(m_slots[i].mtime, ts, sizeof(ts));
            } else {
                std::snprintf(ts, sizeof(ts), "empty");
            }

            const bool dim = !m_slots[i].exists && (m_state == State::LoadSlots);
            row(i, i == m_slot_index, label, ts, dim);
        }
    } else if (m_state == State::Cheats) {
        if (m_cheats.empty()) {
            nvgFontSize(vg, 18.0f);
            nvgFillColor(vg, kTextDim);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, list_x + list_w * 0.5f, list_y + 64,
                    "no cheats found for this rom", nullptr);
            nvgFontSize(vg, 14.0f);
            char hint_path[160];
            std::snprintf(hint_path, sizeof(hint_path),
                "drop a .cht file at /foyer/cheats/%s/%s.cht",
                m_rom_folder.c_str(), m_rom_stem.c_str());
            nvgText(vg, list_x + list_w * 0.5f, list_y + 90,
                    hint_path, nullptr);
        } else {
            const int visible = (int)((ph - 84 - 28) / row_h);
            // Same scroll-tracking pattern as the core-options view —
            // keep the focused row in the visible window.
            int first = std::max(0, m_cheat_index - visible / 2);
            const int total = (int)m_cheats.size();
            if (first + visible > total) first = std::max(0, total - visible);

            for (int i = 0; i < visible && first + i < total; i++) {
                const auto& c = m_cheats[first + i];
                const bool sel = (first + i == m_cheat_index);
                row(i, sel,
                    c.desc.c_str(),
                    c.enabled ? "ON" : "off",
                    !c.enabled);
            }
        }
    }
}

void Overlay::toast(std::string msg) {
    m_toast    = std::move(msg);
    m_toast_ttl = 90;
}

} // namespace foyer::libretro
