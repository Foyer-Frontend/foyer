#include "emulator_activity.hpp"
#include "emulator_view.hpp"

#include "libretro/frontend.hpp"
#include "libretro/audio.hpp"
#include "libretro/input.hpp"
#include "libretro/video.hpp"
#include "platform/log.hpp"

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
    : m_rom_path(std::move(rom_path)) {}

EmulatorActivity::~EmulatorActivity() {
    if (m_ticker) {
        m_ticker->stop();
        delete m_ticker;
        m_ticker = nullptr;
    }
    auto& fe = foyer::libretro::Frontend::instance();
    if (m_game_ok) fe.unload_game();
    fe.shutdown();
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
    if (!fe.load_game(m_rom_path)) {
        foyer::log::write("[player-brls] load_game(%s) failed\n",
            m_rom_path.c_str());
        brls::Application::quit();
        return;
    }
    m_game_ok = true;

    // Per-frame retro_run + view invalidation. The ticker fires on
    // brls's main thread, so calls into retro_run + the video sink
    // are race-free with the draw path.
    if (!m_ticker) {
        m_ticker = new FrameTicker([this]() { this->tick_frame(); });
        m_ticker->start();
    }

    // B = pause / quit. Pause overlay isn't ported to brls yet —
    // wire B to quit so users have a clean exit path.
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Quit", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::quit();
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

void EmulatorActivity::tick_frame() {
    auto& fe = foyer::libretro::Frontend::instance();
    fe.run_frame();
    if (m_view) m_view->invalidate();
}

}  // namespace foyer::player
