// foyer player entry — Phase 2.
//
// Boots a rom, runs retro_run() each frame, draws the core's video output
// aspect-fit on screen, and forwards Switch pad to libretro input.
// Audio + pause overlay land in the next phase.
//
// argv layout (set by the browser via envSetNextLoad):
//   argv[0]  = nro path on sd ("sdmc:/switch/foyer/cores/foyer-fceumm.nro")
//   argv[1]  = rom path on sd ("sdmc:/roms/nes/Some Game.nes")

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <string_view>
#include <sys/stat.h>

#include "platform/app.hpp"
#include "platform/log.hpp"
#include "library/system_db.hpp"
#include "libretro/frontend.hpp"
#include "util/archive.hpp"
#include "libretro/video.hpp"
#include "libretro/audio.hpp"
#include "libretro/input.hpp"
#include "libretro/overlay.hpp"
#include "libretro/bezel.hpp"
#include "libretro/savestate.hpp"
#include "libretro/cheevos.hpp"

#include <nanovg.h>

namespace {

// Strip the "sdmc:" devoptab prefix that hbloader argv brings along — POSIX
// stdlib opens want the bare path.
std::string normalise_argv_path(std::string_view in) {
    if (in.starts_with("\"") && in.ends_with("\"")) {
        in = in.substr(1, in.size() - 2);
    }
    if (in.starts_with("sdmc:")) {
        in = in.substr(5);
    }
    return std::string{in};
}

bool ends_with_lower(const char* name, const char* suffix) {
    const auto nl = std::strlen(name);
    const auto sl = std::strlen(suffix);
    if (nl < sl) return false;
    for (std::size_t i = 0; i < sl; i++) {
        char a = name[nl - sl + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != suffix[i]) return false;
    }
    return true;
}

// Dev-only fallback: scan /foyer/roms/<system>/ for the first file whose
// extension the core accepts. Lets the user double-click the player nro
// from hbmenu and have it auto-pick a rom, no argv plumbing required.
std::string find_default_rom(const std::string& valid_extensions) {
#if defined(FOYER_CORE_NAME)
#  define FOYER_STR2(x) #x
#  define FOYER_STR(x)  FOYER_STR2(x)
    const char* core_name = FOYER_STR(FOYER_CORE_NAME);
#  undef FOYER_STR
#  undef FOYER_STR2
#else
    const char* core_name = "fceumm";
#endif

    // Look up the owning system for this core via the shared system db.
    std::string sys_dir = "/foyer/roms";
    if (auto lookup = foyer::library::find_core(core_name); lookup.sys) {
        sys_dir = std::string{"/foyer/roms/"}
                + std::string{lookup.sys->folder_name};
    }

    auto* dir = ::opendir(sys_dir.c_str());
    if (!dir) return {};

    std::string picked;
    while (auto* e = ::readdir(dir)) {
        if (!e->d_name[0] || e->d_name[0] == '.') continue;
        if (e->d_type != DT_REG) continue;

        // Match the file's extension against the core's "|"-separated list.
        std::string_view exts{valid_extensions};
        std::size_t cursor = 0;
        bool ok = false;
        while (cursor < exts.size()) {
            auto bar = exts.find('|', cursor);
            const auto len = (bar == std::string_view::npos) ? exts.size() - cursor : bar - cursor;
            std::string suffix = ".";
            suffix.append(exts.data() + cursor, len);
            if (ends_with_lower(e->d_name, suffix.c_str())) {
                ok = true;
                break;
            }
            if (bar == std::string_view::npos) break;
            cursor = bar + 1;
        }
        if (!ok) continue;

        picked = sys_dir + "/" + e->d_name;
        break;
    }
    ::closedir(dir);
    return picked;
}

void draw_idle(NVGcontext* vg, float w, float h, const char* msg) {
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBA(0x05, 0x05, 0x06, 0xFF));
    nvgFill(vg);

    nvgFontSize(vg, 28.0f);
    nvgFillColor(vg, nvgRGBA(0xCC, 0xCC, 0xCC, 0xFF));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, w / 2.0f, h / 2.0f, msg, nullptr);
}

} // namespace

int main(int argc, char** argv) {
    foyer::platform::App app;
    auto& fe = foyer::libretro::Frontend::instance();

    // Hook the platform's draw callback to render core output.
    foyer::libretro::VideoSinkImpl::instance().init(app.vg());

    if (!fe.init()) {
        foyer::log::write("[player] frontend init failed\n");
        return 1;
    }

    std::string rom_path;
    if (argc >= 2) {
        rom_path = normalise_argv_path(argv[1]);
    } else {
        rom_path = find_default_rom(fe.system_info().valid_extensions);
        if (rom_path.empty()) {
            foyer::log::write("[player] no argv and no rom found in /foyer/roms\n");
            app.set_draw_fn([](NVGcontext* vg, float w, float h) {
                draw_idle(vg, w, h,
                    "drop a rom into /foyer/roms/<system>/ and relaunch");
            });
            while (app.tick()) {}
            fe.shutdown();
            return 1;
        }
        foyer::log::write("[player] auto-picked %s\n", rom_path.c_str());
    }

    // Archive support: if the user picked a .zip / .7z, extract the first
    // matching inner rom into /foyer/data/extract/<stem>.<ext> and load
    // that path instead. The core only sees a regular file on disk.
    auto ends_with_ci = [](std::string_view s, std::string_view suf) {
        if (s.size() < suf.size()) return false;
        for (std::size_t i = 0; i < suf.size(); i++) {
            char a = s[s.size() - suf.size() + i];
            char b = suf[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) return false;
        }
        return true;
    };
    if (ends_with_ci(rom_path, ".zip") || ends_with_ci(rom_path, ".7z")) {
        const std::string& valid = fe.system_info().valid_extensions;
        const auto inner = foyer::util::archive_peek_inner_rom(rom_path, valid);
        if (inner.empty()) {
            foyer::log::write("[player] archive %s holds no compatible rom\n",
                rom_path.c_str());
        } else {
            // Use just the basename of the inner entry for the temp file.
            const auto slash = inner.rfind('/');
            const std::string inner_base = (slash == std::string::npos)
                ? inner : inner.substr(slash + 1);
            const std::string out = "/foyer/data/extract/" + inner_base;
            ::mkdir("/foyer/data", 0777);
            ::mkdir("/foyer/data/extract", 0777);
            if (foyer::util::archive_extract_inner_rom(rom_path, valid, out)) {
                foyer::log::write("[player] extracted %s -> %s\n",
                    inner.c_str(), out.c_str());
                rom_path = out;
            } else {
                foyer::log::write("[player] failed to extract %s from %s\n",
                    inner.c_str(), rom_path.c_str());
            }
        }
    }

    if (!fe.load_game(rom_path)) {
        foyer::log::write("[player] load_game failed; idling\n");
        app.set_draw_fn([rom_path](NVGcontext* vg, float w, float h) {
            draw_idle(vg, w, h, "load failed");
        });
        while (app.tick()) {}
        fe.shutdown();
        return 1;
    }

    // Optional argv[3] = "resume=<slot>" — load the save state once the rom
    // has booted so the user picks up where they left off.
    int resume_slot = -1;
    for (int i = 2; i < argc; i++) {
        if (!argv[i]) continue;
        const auto raw = normalise_argv_path(argv[i]);
        if (raw.rfind("resume=", 0) == 0) {
            const auto n = std::atoi(raw.c_str() + 7);
            if (n >= 0 && n < foyer::libretro::kStateSlotCount) resume_slot = n;
        }
    }
    if (resume_slot >= 0) {
        const auto path = foyer::libretro::state_path_for(rom_path, {}, resume_slot);
        if (foyer::libretro::load_state(path)) {
            foyer::log::write("[player] auto-resumed slot %d\n", resume_slot);
        } else {
            foyer::log::write("[player] auto-resume slot %d failed\n", resume_slot);
        }
    }

    // Hook audio at the core's reported sample rate. Voice format = stereo S16.
    foyer::libretro::AudioSink::instance().init((unsigned)fe.sample_rate());

    // Pause overlay (Phase 4 polish: 10 timestamped slots).
    foyer::libretro::Overlay overlay;
    const std::string  rom_for_slots = rom_path;
    // Tell the overlay which rom is loaded so the Cheats sub-screen
    // can resolve /foyer/cheats/<system>/<stem>.cht. We derive the
    // system folder from the rom path's parent directory and the
    // stem from its filename.
    {
        std::string folder, stem;
        const auto last_slash = rom_path.find_last_of('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            const auto parent = rom_path.substr(0, last_slash);
            const auto p2     = parent.find_last_of('/');
            folder = (p2 == std::string::npos) ? parent : parent.substr(p2 + 1);
            stem   = rom_path.substr(last_slash + 1);
            const auto dot = stem.find_last_of('.');
            if (dot != std::string::npos) stem = stem.substr(0, dot);
        }
        overlay.set_rom(folder, stem);
    }
    overlay.set_hooks({
        .get_aspect = []() { return foyer::libretro::VideoSinkImpl::instance().aspect(); },
        .set_aspect = [](foyer::libretro::AspectMode m) {
            foyer::libretro::VideoSinkImpl::instance().set_aspect(m);
        },
        .probe_slots = [&rom_for_slots](foyer::libretro::StateSlot out[foyer::libretro::kStateSlotCount]) {
            foyer::libretro::inspect_slots(rom_for_slots, {}, out);
        },
    });

    // RetroAchievements (no-op if creds aren't filled in accounts.jsonc).
    auto& cheevos = foyer::libretro::Cheevos::instance();
    cheevos.init([&overlay](const std::string& title) {
        overlay.toast(title);
    });

    // Tell the cheevos client where to write progress so the browser can
    // surface "X/Y achievements" without making any network calls itself.
    // rom_path looks like /foyer/roms/<system>/<stem>.<ext>; we derive the
    // sidecar coordinates from that.
    {
        std::string_view p{rom_path};
        const auto last_slash = p.rfind('/');
        const std::string_view file =
            (last_slash == std::string_view::npos) ? p : p.substr(last_slash + 1);
        const auto dot  = file.rfind('.');
        const std::string stem{(dot == std::string_view::npos) ? file : file.substr(0, dot)};

        std::string sys_folder;
        if (last_slash != std::string_view::npos) {
            const auto parent = p.substr(0, last_slash);
            const auto pslash = parent.rfind('/');
            sys_folder = std::string{
                (pslash == std::string_view::npos) ? parent : parent.substr(pslash + 1)};
        }
        cheevos.set_progress_sidecar(sys_folder, stem);
    }

    cheevos.identify_game(rom_path);

    // Tell the bezel module which rom is loaded so it can pick up
    // /foyer/bezels/<folder>/<stem>.png (per-rom) or
    // /foyer/bezels/<folder>.png (per-system) if either exists.
    {
        std::string folder, stem;
        const auto last_slash = rom_path.find_last_of('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            const auto parent = rom_path.substr(0, last_slash);
            const auto p2     = parent.find_last_of('/');
            folder = (p2 == std::string::npos) ? parent : parent.substr(p2 + 1);
            stem   = rom_path.substr(last_slash + 1);
            const auto dot = stem.find_last_of('.');
            if (dot != std::string::npos) stem = stem.substr(0, dot);
        }
        foyer::libretro::set_bezel_rom_id(folder, stem);
    }

    // Once a game is loaded, the per-frame draw is the core's framebuffer
    // composited with the (possibly hidden) pause overlay. Bezel art (if
    // present) is sandwiched between them so it sits over the emulator
    // output but UNDER the pause menu.
    app.set_draw_fn([&overlay](NVGcontext* vg, float w, float h) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, w, h);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 0xFF));
        nvgFill(vg);
        foyer::libretro::VideoSinkImpl::instance().draw(vg, w, h);
        foyer::libretro::draw_bezel(vg, w, h);
        overlay.draw(vg, w, h);
    });

    while (app.tick()) {
        const auto held = padGetButtons(&app.pad());
        const auto down = padGetButtonsDown(&app.pad());

        // Forward touch into the overlay so the in-game pause menu
        // is finger-tappable on handheld. No-op when the overlay is
        // hidden; the core itself doesn't see touches when paused.
        const auto& t = app.touch();
        foyer::libretro::OverlayTouch ot{};
        if (t.count > 0) {
            ot.tap_started = t.tap_started;
            ot.x = t.points[0].x;
            ot.y = t.points[0].y;
        }
        const auto act = overlay.update(held, down, ot,
                                        (float)app.width(), (float)app.height());
        switch (act) {
            case foyer::libretro::Overlay::Action::SaveStateSlot: {
                const auto slot = overlay.last_slot();
                const auto path = foyer::libretro::state_path_for(rom_for_slots, {}, slot);
                char msg[64];
                if (foyer::libretro::save_state(path)) {
                    std::snprintf(msg, sizeof(msg),
                        slot == 0 ? "Saved Quick" : "Saved Slot %d", slot);
                } else {
                    std::snprintf(msg, sizeof(msg), "Save failed");
                }
                overlay.toast(msg);
            } break;
            case foyer::libretro::Overlay::Action::LoadStateSlot: {
                const auto slot = overlay.last_slot();
                const auto path = foyer::libretro::state_path_for(rom_for_slots, {}, slot);
                char msg[64];
                if (foyer::libretro::load_state(path)) {
                    std::snprintf(msg, sizeof(msg),
                        slot == 0 ? "Loaded Quick" : "Loaded Slot %d", slot);
                } else {
                    std::snprintf(msg, sizeof(msg), "Load failed");
                }
                overlay.toast(msg);
            } break;
            case foyer::libretro::Overlay::Action::Quit:
                app.quit();
                break;
            default: break;
        }

        if (overlay.is_open()) {
            // Don't feed buttons to the core or run a frame; just keep
            // drawing the last framebuffer + overlay on top.
            continue;
        }

        foyer::libretro::poll_input(app.pad());
        fe.run_frame();
        foyer::libretro::Cheevos::instance().do_frame();
        foyer::libretro::AudioSink::instance().pump();
    }

    foyer::libretro::Cheevos::instance().shutdown();
    fe.unload_game();
    fe.shutdown();
    foyer::libretro::AudioSink::instance().shutdown();
    foyer::libretro::VideoSinkImpl::instance().shutdown();

    // Chain back to the foyer browser. Resolution order:
    //   1. argv[2] passed by the browser (correct by construction)
    //   2. /switch/foyer/foyer.nro (sphaira-style nested layout)
    //   3. /switch/foyer.nro       (flat layout)
    auto exists = [](const std::string& sd_path) {
        struct stat st{};
        return ::stat(sd_path.c_str(), &st) == 0;
    };

    std::string back;
    if (argc >= 3 && argv[2] && argv[2][0]) {
        const auto p = normalise_argv_path(argv[2]); // drops "sdmc:" + quotes
        if (exists(p)) back = "sdmc:" + p;
    }
    if (back.empty() && exists("/switch/foyer/foyer.nro")) {
        back = "sdmc:/switch/foyer/foyer.nro";
    }
    if (back.empty() && exists("/switch/foyer.nro")) {
        back = "sdmc:/switch/foyer.nro";
    }

    if (back.empty()) {
        foyer::log::write("[player] no foyer.nro found; not chaining back\n");
    } else {
        char next_argv[300];
        std::snprintf(next_argv, sizeof(next_argv), "\"%s\"", back.c_str());
        Result rc = envSetNextLoad(back.c_str(), next_argv);
        foyer::log::write("[player] chain-back %s rc=0x%X\n", back.c_str(), rc);
    }
    return 0;
}
