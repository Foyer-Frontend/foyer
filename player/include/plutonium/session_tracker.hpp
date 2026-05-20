#pragma once
//
// Per-session writeback from the plutonium player back into the
// browser's per_game.jsonc store. See
// openspec/specs/player-session-writeback/spec.md for the contract.
//
// Process-singleton bound to argv[1]'s rom path. Wallclock-excluding-
// pause measurement. Quit-cell-only trigger. Synchronous writeback
// before envSetNextLoad fires.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace foyer::player::plut {

class SessionTracker {
public:
    static SessionTracker& instance();

    // Bind the tracker to a rom_path and capture per-game baselines
    // for change-detection at finalize(). Idempotent — repeat calls
    // with the same rom are no-ops.
    void start(std::string_view rom_path);

    // Per-frame accumulator. Called from EmulatorElement::OnRender.
    // Adds the delta since the previous tick to m_played_micros
    // unless paused or the delta exceeds the suspend-clamp.
    void tick();

    // Pause-state setter — wired from MainApplication::TogglePause
    // (the actual EmulatorElement::SetPaused mutator). Drains any
    // pending pre-toggle delta into the accumulator before flipping
    // the gate so single-frame transitions don't double-count.
    void set_paused(bool paused);

    // Persist the session to per_game.jsonc via the existing
    // foyer::library mutators. Idempotent. Synchronous — by the
    // time this returns, the file has been atomically rewritten
    // for each field.
    void finalize();

    const std::string& rom_path() const { return m_rom_path; }

private:
    SessionTracker() = default;

    using Clock = std::chrono::steady_clock;

    bool         m_started   = false;
    bool         m_finalised = false;
    bool         m_paused    = false;

    std::string  m_rom_path;
    Clock::time_point m_last_tick{};

    // Microseconds — converted to whole seconds at finalize.
    std::uint64_t m_played_micros = 0;

    // Change-detection baselines.
    std::string  m_start_per_game_shader;
    std::string  m_start_general_shader;
    int          m_start_per_game_runahead = -1;
    int          m_start_general_runahead  = -1;
};

}  // namespace foyer::player::plut
