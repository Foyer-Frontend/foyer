#include "cheevos.hpp"
#include "frontend.hpp"
#include "library/game_meta.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "scrapers/accounts.hpp"

#include <cstring>
#include <fstream>
#include <vector>

#include <rc_client.h>
#include <rc_consoles.h>

extern "C" {
    void* retro_get_memory_data(unsigned id);
    size_t retro_get_memory_size(unsigned id);
}

#define RETRO_MEMORY_SYSTEM_RAM 2

namespace foyer::libretro {
namespace {

void log_callback(const char* message, const rc_client_t* /*c*/) {
    foyer::log::write("[ra] %s\n", message);
}

// rcheevos asks the host to perform an HTTP request. We translate to libcurl
// via the shared/net helpers and call the supplied callback with the response.
void server_call(const rc_api_request_t* req, rc_client_server_callback_t cb,
                 void* userdata, rc_client_t* /*client*/) {
    rc_api_server_response_t r{};
    if (req->post_data && *req->post_data) {
        const auto resp = foyer::net::post_form(req->url, req->post_data);
        r.body         = resp.body.data();
        r.body_length  = resp.body.size();
        r.http_status_code = (int)resp.code;
    } else {
        const auto resp = foyer::net::get(req->url);
        r.body         = resp.body.data();
        r.body_length  = resp.body.size();
        r.http_status_code = (int)resp.code;
    }
    cb(&r, userdata);
}

// Memory callback. rcheevos hands us an offset into "the system RAM"; we
// translate via the libretro memory map for the running core.
uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                     rc_client_t* /*client*/) {
    auto* mem = static_cast<const std::uint8_t*>(
                    retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    const auto sz = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (!mem || address + num_bytes > sz) return 0;
    std::memcpy(buffer, mem + address, num_bytes);
    return num_bytes;
}

void event_handler(const rc_client_event_t* ev, rc_client_t* /*client*/) {
    auto& self = Cheevos::instance();
    if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED) {
        if (ev->achievement && ev->achievement->title) {
            Cheevos::Unlock u;
            u.title       = ev->achievement->title;
            u.description = ev->achievement->description
                ? ev->achievement->description : "";
            u.points      = (int)ev->achievement->points;
            u.badge_url   = ev->achievement->badge_url
                ? ev->achievement->badge_url : "";
            foyer::log::write("[ra] unlocked: %s (%d pts)\n",
                u.title.c_str(), u.points);
            self.on_unlock_full(u);
        }
    } else if (ev->type == RC_CLIENT_EVENT_GAME_COMPLETED) {
        Cheevos::Unlock u;
        u.title = "All achievements unlocked!";
        self.on_unlock_full(u);
    }
}

void on_login_done(int result, const char* error_message,
                   rc_client_t* client, void* /*userdata*/) {
    if (result != RC_OK) {
        foyer::log::write("[ra] login failed: %s\n",
            error_message ? error_message : "unknown");
        return;
    }
    foyer::log::write("[ra] logged in as %s\n",
        rc_client_get_user_info(client) ? rc_client_get_user_info(client)->display_name : "?");
}

void on_load_done(int result, const char* error_message,
                  rc_client_t* client, void* /*userdata*/) {
    auto& self = Cheevos::instance();
    if (result != RC_OK) {
        foyer::log::write("[ra] load failed: %s\n",
            error_message ? error_message : "unknown");
        self.mark_failed();
        return;
    }
    auto* info = rc_client_get_game_info(client);
    int total = 0;
    auto* list = rc_client_create_achievement_list(client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list) {
        // Match Cheevos::list()'s filter — RA injects a 101000001
        // "Warning: Unsupported Emulator" placeholder which we don't
        // surface in the picker, so it shouldn't count toward the
        // "N/M" badge on the pause-root row either.
        constexpr uint32_t kRaUnsupportedEmulatorId = 101000001;
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            const auto& bk = list->buckets[b];
            for (uint32_t i = 0; i < bk.num_achievements; ++i) {
                const auto* a = bk.achievements[i];
                if (!a || a->id == kRaUnsupportedEmulatorId) continue;
                total++;
            }
        }
        rc_client_destroy_achievement_list(list);
    }
    self.mark_loaded(total);
    foyer::log::write("[ra] game: %s — %d cheevos\n",
        info && info->title ? info->title : "?", total);
}

} // namespace

Cheevos& Cheevos::instance() {
    static Cheevos g;
    return g;
}

bool Cheevos::init(NotifyCb on_unlock) {
    m_on_unlock = std::move(on_unlock);

    const auto& a = scrapers::accounts().retroachievements;
    if (!a.ready()) {
        foyer::log::write("[ra] credentials missing in accounts.jsonc — skipping\n");
        return false;
    }

    auto* client = rc_client_create(read_memory, server_call);
    if (!client) {
        foyer::log::write("[ra] rc_client_create failed\n");
        return false;
    }
    m_client = client;

    rc_client_set_event_handler(client, event_handler);
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_INFO, log_callback);
    rc_client_set_hardcore_enabled(client, 0);

    // Prefer password login when the user filled in the password
    // field. rcheevos exchanges it for a session token on first
    // request — the alternative `rc_client_begin_login_with_token`
    // expects a pre-obtained Connect API Token, NOT the "Web API
    // Key" on RA's settings page (those are different secrets and
    // login_with_token silently fails when fed the Web API Key).
    if (!a.password.empty()) {
        foyer::log::write("[ra] login via password as %s\n", a.user.c_str());
        rc_client_begin_login_with_password(client,
            a.user.c_str(), a.password.c_str(),
            on_login_done, nullptr);
    } else {
        foyer::log::write("[ra] login via token as %s\n", a.user.c_str());
        rc_client_begin_login_with_token(client,
            a.user.c_str(), a.token.c_str(),
            on_login_done, nullptr);
    }
    return true;
}

void Cheevos::shutdown() {
    if (!m_client) return;
    auto* client = static_cast<rc_client_t*>(m_client);
    rc_client_destroy(client);
    m_client = nullptr;
    m_active = false;
}

bool Cheevos::identify_game(const std::string& rom_path) {
    if (!m_client) return false;

    // Slurp the rom into memory. rcheevos hashes it internally — for raw
    // formats it's CRC-style; for archive formats the hash differs.
    std::ifstream in{rom_path, std::ios::binary};
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto sz = (std::size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (!in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)sz)) {
        return false;
    }

    auto* client = static_cast<rc_client_t*>(m_client);
    rc_client_begin_identify_and_load_game(
        client, RC_CONSOLE_UNKNOWN,
        rom_path.c_str(), buf.data(), buf.size(),
        on_load_done, nullptr);
    return true;
}

void Cheevos::do_frame() {
    if (!m_client) return;
    rc_client_do_frame(static_cast<rc_client_t*>(m_client));
}

void Cheevos::set_progress_sidecar(std::string system_folder, std::string rom_stem) {
    m_sidecar_sys  = std::move(system_folder);
    m_sidecar_stem = std::move(rom_stem);
}

void Cheevos::persist_progress() const {
    if (m_sidecar_sys.empty() || m_sidecar_stem.empty()) return;
    auto meta = library::load_meta(m_sidecar_sys, m_sidecar_stem);
    meta.cheevos_total    = m_total;
    meta.cheevos_unlocked = m_unlocked;
    library::save_meta(m_sidecar_sys, m_sidecar_stem, meta);
}

void Cheevos::on_unlock_full(const Unlock& ev) {
    m_unlocked++;
    if (m_on_unlock) m_on_unlock(ev);
    persist_progress();
}

void Cheevos::mark_loaded(int total) {
    m_active   = true;
    m_total    = total;
    // Pull the actual already-unlocked count from rcheevos so the browser
    // sees "5/25" instead of always "0/25" for revisited games. Falls back
    // to 0 if the API isn't available on this client.
    if (m_client) {
        rc_client_user_game_summary_t sum{};
        rc_client_get_user_game_summary(static_cast<rc_client_t*>(m_client), &sum);
        m_unlocked = (int)sum.num_unlocked_achievements;
    } else {
        m_unlocked = 0;
    }
    persist_progress();
}

void Cheevos::mark_failed() {
    m_active = false;
}

std::vector<Cheevos::AchievementRow> Cheevos::list() const {
    std::vector<AchievementRow> out;
    if (!m_client) return out;
    auto* client = static_cast<rc_client_t*>(m_client);
    auto* lst = rc_client_create_achievement_list(client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!lst) return out;

    // Bucket layout already orders LOCKED before UNLOCKED, then by
    // recency within each bucket. We flip the visible order so the
    // user sees their earned achievements first.
    // RA injects a synthetic 0-pt achievement with id 101000001
    // titled "Warning: Unsupported Emulator" into every game's set
    // when the client's User-Agent isn't on their allowlist. foyer
    // isn't (yet) — filed-and-pending. Hardcore is already off in
    // init(), so the warning's "Hardcore unlocks cannot be earned
    // using this emulator" description doesn't apply to us; filter
    // the row out of the picker so it doesn't confuse the user.
    constexpr uint32_t kRaUnsupportedEmulatorId = 101000001;
    auto append_bucket = [&](int target_state) {
        for (uint32_t b = 0; b < lst->num_buckets; ++b) {
            const auto& bk = lst->buckets[b];
            if ((int)bk.bucket_type != target_state) continue;
            for (uint32_t i = 0; i < bk.num_achievements; ++i) {
                const auto* a = bk.achievements[i];
                if (!a) continue;
                if (a->id == kRaUnsupportedEmulatorId) continue;
                AchievementRow r;
                r.title       = a->title       ? a->title       : "";
                r.description = a->description ? a->description : "";
                r.points      = (int)a->points;
                r.unlocked    = a->unlocked != 0;
                out.push_back(std::move(r));
            }
        }
    };
    append_bucket(RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED);
    append_bucket(RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED);
    append_bucket(RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED);
    append_bucket(RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED);

    rc_client_destroy_achievement_list(lst);
    return out;
}

} // namespace foyer::libretro
