#include "emulator_activity.hpp"
#include "emulator_view.hpp"
#include "pause_activity.hpp"

#include "libretro/frontend.hpp"
#include "libretro/audio.hpp"
#include "libretro/input.hpp"
#include "libretro/video.hpp"
#include "platform/log.hpp"
#include "util/archive.hpp"

#include <switch.h>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

using namespace brls::literals;

namespace foyer::player {

namespace {

// brls's frame cadence is decoupled from the core's fps. The
// ticker pins ~16 ms (≈60 Hz); cores that report 50 fps simply
// skip a tick every six. The retro_run audio sink absorbs drift.
class FrameTicker : public brls::RepeatingTask {
public:
    using Body = std::function<void()>;
    explicit FrameTicker(Body body)
        : brls::RepeatingTask(16), body(std::move(body)) {}
    void run() override { body(); }
private:
    Body body;
};

}  // namespace

EmulatorActivity::EmulatorActivity(std::string rom_path)
    : m_rom_path(rom_path), m_original_rom_path(rom_path) {
    // /foyer/roms/<sys>/<file> -> sys is the parent dir name.
    // Used by libretro::state_path_for to route .state slots
    // under /foyer/states/<sys>/.
    const auto slash = m_original_rom_path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        const auto upper = m_original_rom_path.substr(0, slash);
        const auto up_slash = upper.find_last_of('/');
        m_system_folder = (up_slash == std::string::npos)
            ? upper : upper.substr(up_slash + 1);
    }
}

EmulatorActivity::~EmulatorActivity() {
    if (m_ticker) {
        m_ticker->stop();
        delete m_ticker;
        m_ticker = nullptr;
    }
    foyer::libretro::AudioSink::instance().shutdown();
    auto& fe = foyer::libretro::Frontend::instance();
    if (m_game_ok) fe.unload_game();
    fe.shutdown();

    // Don't unlink the extracted rom — keeping it around lets a
    // hot game skip the re-extract on its next launch. Stale
    // files are scrubbed by the browser at boot via
    // self_update::scrub_extract_lru() (>10 days untouched).
}

brls::View* EmulatorActivity::createContentView() {
    m_view = new EmulatorView();
    return m_view;
}

void EmulatorActivity::onContentAvailable() {
    auto& fe = foyer::libretro::Frontend::instance();

    // Hand the brls nanovg context to the video sink so retro_run's
    // video_refresh callback uploads into a texture brls can draw.
    foyer::libretro::VideoSinkImpl::instance().init(
        brls::Application::getNVGContext());

    if (!fe.init()) {
        foyer::log::write("[player-brls] frontend init failed\n");
        brls::Application::quit();
        return;
    }

    // Archive support — mirrors legacy main.cpp's path. Cores
    // refuse .zip / .7z directly; extract the first matching
    // inner rom into /foyer/data/extract/ and load that.
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
    if (ends_with_ci(m_rom_path, ".zip")
        || ends_with_ci(m_rom_path, ".7z")) {
        const std::string& valid = fe.system_info().valid_extensions;
        const auto inner = foyer::util::archive_peek_inner_rom(
            m_rom_path, valid);
        if (inner.empty()) {
            foyer::log::write(
                "[player-brls] archive %s holds no compatible rom\n",
                m_rom_path.c_str());
        } else {
            const auto slash = inner.rfind('/');
            const std::string inner_base = (slash == std::string::npos)
                ? inner : inner.substr(slash + 1);
            const std::string out = "/foyer/data/extract/" + inner_base;
            ::mkdir("/foyer/data", 0777);
            ::mkdir("/foyer/data/extract", 0777);

            // Reuse if a prior session already extracted this
            // file — saves unzipping ~MB on every hot-game
            // launch. Touch the file so the LRU scrub on next
            // boot sees recent mtime.
            struct stat st{};
            const bool cached = (::stat(out.c_str(), &st) == 0
                                 && S_ISREG(st.st_mode)
                                 && st.st_size > 0);
            if (cached) {
                foyer::log::write(
                    "[player-brls] reusing cached extract %s\n",
                    out.c_str());
                ::utime(out.c_str(), nullptr);
                m_rom_path = out;
            } else if (foyer::util::archive_extract_inner_rom(
                           m_rom_path, valid, out)) {
                foyer::log::write(
                    "[player-brls] extracted %s -> %s\n",
                    inner.c_str(), out.c_str());
                m_rom_path = out;
            } else {
                foyer::log::write(
                    "[player-brls] failed to extract %s from %s\n",
                    inner.c_str(), m_rom_path.c_str());
            }
        }
    }

    if (!fe.load_game(m_rom_path)) {
        foyer::log::write("[player-brls] load_game(%s) failed\n",
            m_rom_path.c_str());
        brls::Application::quit();
        return;
    }
    m_game_ok = true;

    // Pin SRAM naming to the original rom path (the .zip / .7z
    // the launcher handed us) so the .srm survives extract dir
    // churn and stays in sync with raw-rom launches of the
    // same game.
    fe.set_sram_basis_path(m_original_rom_path);

    // Audio. Sample rate comes from retro_av_info populated by
    // load_game above; AudioSink owns its own libnx audren
    // service + worker thread.
    if (!foyer::libretro::AudioSink::instance().init(
            (unsigned)fe.sample_rate())) {
        foyer::log::write(
            "[player-brls] audio init failed @ %u Hz — silent run\n",
            (unsigned)fe.sample_rate());
    }

    // Per-frame retro_run + view invalidation. The ticker fires on
    // brls's main thread, so calls into retro_run + the video sink
    // are race-free with the draw path.
    if (!m_ticker) {
        m_ticker = new FrameTicker([this]() { this->tick_frame(); });
        m_ticker->start();
    }

    if (auto* cv = this->getContentView()) {
        // Eat every button brls might otherwise consume so the
        // core sees the press through its own libnx pad poll
        // in shared/libretro/input.cpp. Includes + and -
        // (BUTTON_START / BUTTON_BACK) which the user wants
        // routed straight to the libretro Start / Select.
        // Hidden + soundless so the brls hint bar / click SFX
        // never trip.
        auto swallow = [](brls::View*) { return true; };
        const brls::ControllerButton kSwallow[] = {
            brls::BUTTON_A,     brls::BUTTON_B,
            brls::BUTTON_X,     brls::BUTTON_Y,
            brls::BUTTON_LB,    brls::BUTTON_RB,
            brls::BUTTON_LT,    brls::BUTTON_RT,
            brls::BUTTON_UP,    brls::BUTTON_DOWN,
            brls::BUTTON_LEFT,  brls::BUTTON_RIGHT,
            brls::BUTTON_START, brls::BUTTON_BACK,
            brls::BUTTON_LSB,   brls::BUTTON_RSB,
        };
        for (auto b : kSwallow) {
            cv->registerAction("", b, swallow,
                /*repeating=*/true, /*hidden=*/true,
                brls::SOUND_NONE);
        }
    }
}

void EmulatorActivity::tick_frame() {
    // Skip the whole frame while the pause overlay is on top —
    // the activity stack's top changes as the user navigates,
    // and we don't want to keep advancing the core (or its
    // audio output) while paused.
    if (brls::Application::getActivitiesStack().back() != this) {
        return;
    }

    if (!m_pad_inited) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&m_pad);
        m_pad_inited = true;
    }
    padUpdate(&m_pad);

    // L3+R3 hold → push the pause overlay. Detected before the
    // libretro input poll so the combo doesn't also register as
    // L3/R3 inside the running core.
    const u64 held = padGetButtons(&m_pad);
    const bool combo =
        (held & HidNpadButton_StickL) && (held & HidNpadButton_StickR);
    if (combo && !m_pause_pushed) {
        m_pause_pushed = true;
        brls::Application::pushActivity(
            new PauseActivity(m_original_rom_path,
                              m_system_folder,
                              []() {}));
        return;
    } else if (!combo) {
        m_pause_pushed = false;
    }

    // Mirror the Switch pad into the libretro frontend's
    // InputState. Without this, retro_run sees zeroed buttons
    // and the user can't get past a game's title screen.
    foyer::libretro::poll_input(m_pad);

    auto& fe = foyer::libretro::Frontend::instance();
    fe.run_frame();
    if (m_view) m_view->invalidate();
}

}  // namespace foyer::player
