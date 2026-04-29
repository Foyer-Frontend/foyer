#include "cheevos.hpp"
#include "frontend.hpp"
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
            self.on_unlock(std::string{"\xF0\x9F\x8F\x86 "} +
                           ev->achievement->title);
            foyer::log::write("[ra] unlocked: %s\n", ev->achievement->title);
        }
    } else if (ev->type == RC_CLIENT_EVENT_GAME_COMPLETED) {
        self.on_unlock("All achievements unlocked!");
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
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            total += (int)list->buckets[b].num_achievements;
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

    rc_client_begin_login_with_token(client,
        a.user.c_str(), a.token.c_str(),
        on_login_done, nullptr);
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

void Cheevos::on_unlock(const std::string& title) {
    m_unlocked++;
    if (m_on_unlock) m_on_unlock(title);
}

void Cheevos::mark_loaded(int total) {
    m_active   = true;
    m_total    = total;
    m_unlocked = 0;
}

void Cheevos::mark_failed() {
    m_active = false;
}

} // namespace foyer::libretro
