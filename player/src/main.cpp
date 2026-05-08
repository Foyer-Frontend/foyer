// foyer player entry.
//
// Boots a rom, drives retro_run() each frame, draws video aspect-fit,
// pumps audio, and forwards Switch pad + touch to libretro. Hosts the
// pause overlay (save/load/cheats/options/aspect/quit), bezel art,
// post-process shader pipeline, RetroAchievements, and run-ahead.
//
// argv layout (set by the browser via envSetNextLoad):
//   argv[0]  = nro path on sd ("sdmc:/switch/foyer/cores/foyer-fceumm.nro")
//   argv[1]  = rom path on sd ("sdmc:/roms/nes/Some Game.nes")
//   argv[2+] = optional "key=value" hint tokens
//             ("resume=N", "shader=name", "runahead=N")

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <string_view>
#include <sys/stat.h>

#include "platform/app.hpp"
#include "platform/log.hpp"
#include "library/system_db.hpp"
#include "library/per_game.hpp"
#include "libretro/frontend.hpp"
#include "util/archive.hpp"
#include "libretro/video.hpp"
#include "libretro/audio.hpp"
#include "libretro/input.hpp"
#include "libretro/overlay.hpp"
#include "libretro/bezel.hpp"
#include "libretro/shader.hpp"
#include "libretro/savestate.hpp"
#include "libretro/cheevos.hpp"

#include <nanovg.h>

// Linker-resolved against the bundled core. We use these directly for
// run-ahead so we can serialize/unserialize state into a heap buffer
// without touching disk.
extern "C" {
    std::size_t retro_serialize_size(void);
    bool        retro_serialize  (void* data, std::size_t size);
    bool        retro_unserialize(const void* data, std::size_t size);
}

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

// Recursive copy from `src` (typically a romfs:/ path) onto `dst` on
// SD. Preserves directory structure. Existing files at `dst` are
// skipped untouched, so this is cheap on subsequent boots and only
// pays the IO cost the first time. Returns the count of files
// actually written, mostly for the log.
int seed_tree(const std::string& src, const std::string& dst) {
    auto* dir = ::opendir(src.c_str());
    if (!dir) return 0;
    ::mkdir(dst.c_str(), 0777);
    int copied = 0;
    while (auto* e = ::readdir(dir)) {
        if (!e->d_name[0] || e->d_name[0] == '.') continue;
        const std::string s = src + "/" + e->d_name;
        const std::string d = dst + "/" + e->d_name;
        if (e->d_type == DT_DIR) {
            copied += seed_tree(s, d);
            continue;
        }
        if (e->d_type != DT_REG) continue;
        struct stat st{};
        if (::stat(d.c_str(), &st) == 0) continue; // already on SD
        std::FILE* in  = std::fopen(s.c_str(), "rb");
        if (!in) continue;
        std::FILE* out = std::fopen(d.c_str(), "wb");
        if (!out) { std::fclose(in); continue; }
        char buf[64 * 1024];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
            std::fwrite(buf, 1, n, out);
        }
        std::fclose(in);
        std::fclose(out);
        copied++;
    }
    ::closedir(dir);
    return copied;
}

} // namespace

int main(int argc, char** argv) {
    foyer::platform::App app;
    auto& fe = foyer::libretro::Frontend::instance();

    // Hook the platform's draw callback to render core output.
    foyer::libretro::VideoSinkImpl::instance().init(app.vg());

    // Per-core asset seed + system_directory override. PPSSPP needs
    // its `assets/` tree (ppge_atlas.zim, lang/, flash0/, compat.ini,
    // ...) at the libretro system directory; without it tico's init
    // logs "Core system files missing" and crashes when the missing
    // atlas is first dereferenced. The recipe stages the tree into
    // romfs:/ppsspp_assets/; here we copy any new files onto SD and
    // point Frontend at the per-core scoped directory.
#if defined(FOYER_CORE_NAME)
#  define FOYER_STR2(x) #x
#  define FOYER_STR(x)  FOYER_STR2(x)
    if (std::string_view{FOYER_STR(FOYER_CORE_NAME)} == "ppsspp") {
        constexpr const char* kSysDir = "/foyer/system/ppsspp";
        ::mkdir("/foyer/system", 0777);
        ::mkdir(kSysDir, 0777);
        const int seeded = seed_tree("romfs:/ppsspp_assets", kSysDir);
        foyer::log::write("[player] ppsspp assets seeded=%d (sys=%s)\n",
            seeded, kSysDir);

        // Force PPSSPP's native config to start in software rendering
        // BEFORE any GL surface gets created. The libretro option
        // ppsspp_software_rendering=enabled is honoured but only
        // checked AFTER PPSSPP has already initialised the GLES
        // backend — which is where the Switch nouveau Mesa 20.1
        // GLES 3.1 stack crashes (PC into a non-code address inside
        // PPSSPP's renderer init). Writing GraphicsBackend=4
        // (Software) to the .ini intercepts the choice during
        // NativeInit, before any eglCreateContext / context_reset.
        // Only written if the file doesn't already exist so the user
        // can flip it back via PPSSPP's in-app menu if Mesa ever
        // gets fixed.
        const std::string ppsspp_ini =
            std::string{kSysDir} + "/PSP/SYSTEM/ppsspp.ini";
        struct stat st{};
        if (::stat(ppsspp_ini.c_str(), &st) != 0) {
            ::mkdir((std::string{kSysDir} + "/PSP").c_str(), 0777);
            ::mkdir((std::string{kSysDir} + "/PSP/SYSTEM").c_str(), 0777);
            if (auto* f = std::fopen(ppsspp_ini.c_str(), "w")) {
                std::fprintf(f,
                    "[Graphics]\n"
                    "GraphicsBackend = 4\n"   // 4 = Software
                    "SoftwareRenderer = True\n"
                    "InternalResolution = 1\n"
                    "FrameSkip = 0\n");
                std::fclose(f);
                foyer::log::write(
                    "[player] wrote ppsspp.ini forcing software render\n");
            }
        }
        fe.set_system_directory(kSysDir);
    }
#  undef FOYER_STR
#  undef FOYER_STR2
#endif

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

    // Stamp last_played now and remember session start so we can write
    // playtime back when the user quits cleanly. The browser uses both
    // fields to drive the Recent virtual system and Sort-by-Playtime.
    foyer::library::mark_per_game_played(rom_path);
    const std::time_t session_start = std::time(nullptr);

    // Optional argv tokens:
    //   "resume=<slot>"   — load a save state right after boot
    //   "shader=<name>"   — bring up the post-process shader pipeline
    //   "runahead=<int>"  — lookahead frames for input-lag reduction
    int         resume_slot     = -1;
    std::string shader_name;
    int         runahead_frames = 0;
    for (int i = 2; i < argc; i++) {
        if (!argv[i]) continue;
        const auto raw = normalise_argv_path(argv[i]);
        if (raw.rfind("resume=", 0) == 0) {
            const auto n = std::atoi(raw.c_str() + 7);
            if (n >= 0 && n < foyer::libretro::kStateSlotCount) resume_slot = n;
        } else if (raw.rfind("shader=", 0) == 0) {
            shader_name = raw.substr(7);
        } else if (raw.rfind("runahead=", 0) == 0) {
            int n = std::atoi(raw.c_str() + 9);
            if (n < 0) n = 0;
            if (n > 4) n = 4;
            runahead_frames = n;
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
    if (!shader_name.empty() && shader_name != "none") {
        // Lazy init — set_preset() calls init() if needed. EGL setup
        // failure logs but is otherwise non-fatal: process() falls back
        // to no-op so the game still renders.
        foyer::libretro::shader_pipeline().set_preset(shader_name);
    }

    // Hook audio at the core's reported sample rate. Voice format = stereo S16.
    foyer::libretro::AudioSink::instance().init((unsigned)fe.sample_rate());

    // Run-ahead bring-up. We call retro_serialize_size() now, after
    // load_game, since most cores only know their state size once a
    // rom is loaded. A 0-byte state means the core can't roll back —
    // we silently downgrade to no run-ahead.
    std::size_t runahead_state_sz = 0;
    void*       runahead_state    = nullptr;
    if (runahead_frames > 0) {
        runahead_state_sz = retro_serialize_size();
        if (runahead_state_sz == 0) {
            foyer::log::write(
                "[runahead] core reports 0-byte state; disabling run-ahead\n");
            runahead_frames = 0;
        } else {
            runahead_state = std::malloc(runahead_state_sz);
            if (!runahead_state) {
                foyer::log::write(
                    "[runahead] state buf alloc(%zu) failed; disabling\n",
                    runahead_state_sz);
                runahead_frames = 0;
            } else {
                foyer::log::write("[runahead] enabled K=%d state=%zu bytes\n",
                    runahead_frames, runahead_state_sz);
            }
        }
    }

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

        if (runahead_frames > 0 && runahead_state) {
            // Run-ahead: emulate K frames into the future without
            // playing audio, then roll back and run one "real" frame
            // (audio on, video frozen on the K-th lookahead frame).
            // Visible video ends up K frames ahead of the audible
            // game state, which masks most of the inherent input
            // delay games carry. Cost: K+1 retro_run() per displayed
            // frame.
            using foyer::libretro::Frontend;
            using foyer::libretro::AudioSink;
            using foyer::libretro::VideoSinkImpl;
            if (retro_serialize(runahead_state, runahead_state_sz)) {
                fe.set_audio_sink(nullptr);
                for (int i = 0; i < runahead_frames; i++) {
                    fe.run_frame();
                }
                fe.set_audio_sink(&AudioSink::on_frame);
                fe.set_video_sink(nullptr);
                if (retro_unserialize(runahead_state, runahead_state_sz)) {
                    fe.run_frame();
                }
                fe.set_video_sink(&VideoSinkImpl::on_frame);
            } else {
                // Serialization failed unexpectedly — fall back to a
                // plain frame. Don't disable run-ahead permanently;
                // some cores reject mid-frame snapshots transiently.
                fe.run_frame();
            }
        } else {
            fe.run_frame();
        }

        foyer::libretro::Cheevos::instance().do_frame();
        foyer::libretro::AudioSink::instance().pump();
    }

    if (runahead_state) std::free(runahead_state);

    // Persist this session's playtime to per_game.jsonc. Anything past
    // 4h (likely a left-running console) is capped to keep the field
    // believable; anything under 5s is treated as a misclick and
    // dropped so we don't pollute Sort-by-Playtime with launch-and-quit
    // sessions.
    {
        const std::time_t now = std::time(nullptr);
        if (now > session_start) {
            std::uint64_t elapsed = (std::uint64_t)(now - session_start);
            if (elapsed < 5)        elapsed = 0;
            if (elapsed > 4 * 3600) elapsed = 4 * 3600;
            if (elapsed > 0) {
                foyer::library::add_per_game_playtime(rom_path, elapsed);
                foyer::log::write("[player] +%llu sec playtime\n",
                    (unsigned long long)elapsed);
            }
        }
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
        // Pass a "foyer-resume" marker token so the browser knows
        // it's a chain-back from a core (not a cold launch from
        // hbmenu) and should restore the saved session view +
        // cursor. Without the marker, foyer treats the boot as
        // cold and lands on Home.
        char next_argv[320];
        std::snprintf(next_argv, sizeof(next_argv),
            "\"%s\" \"foyer-resume\"", back.c_str());
        Result rc = envSetNextLoad(back.c_str(), next_argv);
        foyer::log::write("[player] chain-back %s rc=0x%X\n", back.c_str(), rc);
    }
    return 0;
}
