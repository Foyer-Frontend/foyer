#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace foyer::libretro {

// Thin wrapper around rcheevos' rc_client to plug RetroAchievements into the
// libretro player.
//
// Lifecycle:
//   1. cheevos.init();                       // creates client + login
//   2. cheevos.identify_game(rom_path);      // hashes rom + fetches set
//   3. per frame: cheevos.do_frame();        // checks triggers
//   4. cheevos.shutdown();                   // logout + free
//
// Network IO is synchronous via shared/net/http; fine for the small REST
// calls rcheevos makes around boot. Hash + ID calls happen once per game.
struct Cheevos {
    static Cheevos& instance();

    // Forward decl callback; pause overlay subscribes to display unlock toasts.
    using NotifyCb = std::function<void(const std::string& title)>;

    bool init(NotifyCb on_unlock);
    void shutdown();

    // Identify which sidecar to write progress into. Optional — when empty,
    // unlock counts stay in-memory and aren't persisted for the browser.
    void set_progress_sidecar(std::string system_folder, std::string rom_stem);

    // Hash the rom + ask RA for the achievement set. Returns true if a set
    // was found and active.
    bool identify_game(const std::string& rom_path);

    // Should be called once per video frame after retro_run.
    void do_frame();

    // Total / unlocked counts for HUD display. Both 0 if no set loaded.
    int  total_count()    const { return m_total; }
    int  unlocked_count() const { return m_unlocked; }
    bool active()         const { return m_active; }

    // Mutators reached from the rcheevos C callbacks. Public so the
    // anonymous-namespace trampoline functions in cheevos.cpp can update
    // state without a friend declaration.
    void on_unlock(const std::string& title);
    void mark_loaded(int total);
    void mark_failed();

    // Persist current total/unlocked to the per-game metadata sidecar so
    // the browser can render "X/Y achievements" without making a network
    // call. No-op when the sidecar wasn't set.
    void persist_progress() const;

    // One row in the player's Achievements picker. Snapshot of the
    // rcheevos in-memory set at the moment list() was called — the
    // caller doesn't need to worry about the underlying
    // rc_client_achievement_list_t allocation.
    struct AchievementRow {
        std::string title;
        std::string description;
        int         points    = 0;
        bool        unlocked  = false;
    };

    // Walk rcheevos's achievement set for the currently-loaded game,
    // collapsing the bucket layout into a flat unlocked-first list.
    // Empty when no game is identified or the user isn't logged in.
    std::vector<AchievementRow> list() const;

private:
    Cheevos() = default;

    void* m_client   = nullptr;          // rc_client_t*
    bool  m_active   = false;
    int   m_total    = 0;
    int   m_unlocked = 0;

    std::string m_sidecar_sys;
    std::string m_sidecar_stem;

    NotifyCb m_on_unlock;
};

} // namespace foyer::libretro
