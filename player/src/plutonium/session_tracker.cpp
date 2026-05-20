#include "plutonium/session_tracker.hpp"

#include "library/config.hpp"
#include "library/per_game.hpp"
#include "platform/log.hpp"

namespace foyer::player::plut {

namespace {
// Any single-tick delta longer than this is treated as
// applet-focus-loss / system suspend / scheduler stall and dropped.
// Real ticks at 60 Hz are ~16 ms; the clamp catches anything that
// can't be honest play.
constexpr std::chrono::microseconds kSuspendClamp{1'000'000};
}  // namespace

SessionTracker& SessionTracker::instance() {
    static SessionTracker s;
    return s;
}

void SessionTracker::start(std::string_view rom_path) {
    if (m_started) return;
    m_rom_path  = std::string{rom_path};
    m_last_tick = Clock::now();
    m_started   = true;

    m_start_per_game_shader   = foyer::library::per_game_shader(m_rom_path);
    m_start_general_shader    = foyer::library::config().shader_name;
    m_start_per_game_runahead = foyer::library::per_game_runahead(m_rom_path);
    m_start_general_runahead  = foyer::library::config().runahead_frames;

    foyer::log::write("[session] start rom=%s\n", m_rom_path.c_str());
}

void SessionTracker::tick() {
    if (!m_started || m_finalised) return;
    const auto now = Clock::now();
    if (!m_paused) {
        const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_last_tick);
        if (delta < kSuspendClamp && delta.count() > 0) {
            m_played_micros += static_cast<std::uint64_t>(delta.count());
        }
        // delta >= kSuspendClamp → applet-focus-loss / stall; discard.
    }
    m_last_tick = now;
}

void SessionTracker::set_paused(bool paused) {
    // Drain the pending delta into the accumulator under the
    // CURRENT pause state before toggling the gate, so the frame
    // that straddles the transition lands on the right side.
    tick();
    m_paused = paused;
}

void SessionTracker::finalize() {
    if (!m_started || m_finalised) return;
    tick();
    m_finalised = true;

    const std::uint64_t seconds = m_played_micros / 1'000'000ULL;

    foyer::library::add_per_game_playtime(m_rom_path, seconds);
    foyer::library::mark_per_game_played(m_rom_path);

    // Shader: persist as per-game iff the user actually changed it
    // during the session AND the change isn't already what per-game
    // says. Reading config().shader_name at session end captures any
    // pause-menu pick.
    const std::string& end_shader = foyer::library::config().shader_name;
    if (end_shader != m_start_general_shader
        && end_shader != m_start_per_game_shader) {
        foyer::library::set_per_game_shader(m_rom_path, end_shader);
    }

    // Runahead: same shape. No in-session knob exists today, so the
    // diff is always zero; the code path is here for the day a knob
    // lands.
    const int end_runahead = foyer::library::config().runahead_frames;
    if (end_runahead != m_start_general_runahead
        && end_runahead != m_start_per_game_runahead) {
        foyer::library::set_per_game_runahead(m_rom_path, end_runahead);
    }

    foyer::log::write(
        "[session] finalize rom=%s seconds=%llu\n",
        m_rom_path.c_str(),
        static_cast<unsigned long long>(seconds));
}

}  // namespace foyer::player::plut
