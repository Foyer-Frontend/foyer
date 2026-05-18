#pragma once

#include <string>

namespace foyer::scrapers {

// Credentials read from /foyer/config/accounts.jsonc on first access. The
// JSONC parser strips // line comments before decoding, so users can keep
// notes alongside their keys.
//
// File schema:
//   {
//     "screenscraper":   { "devid": "", "devpassword": "",
//                          "ssid":  "", "sspassword":  "" },
//     "steamgriddb":     { "api_key": "" },
//     "retroachievements": { "user":     "",
//                             "password": "",   // plaintext (preferred)
//                             "token":    "" }  // or pre-obtained connect token
//   }
//
// retroachievements.password is the natural field — paste your RA web
// password and the player exchanges it for a session token on first
// run. retroachievements.token is the legacy field, only useful if
// you already have a Connect API Token from RetroArch's config.
// (Note: the "Web API Key" on the RA settings page is a DIFFERENT
// thing, used only by the REST stats API — it will not log a client
// in. Don't paste that here.)
struct Accounts {
    struct ScreenScraper {
        std::string devid;
        std::string devpassword;
        std::string ssid;
        std::string sspassword;
        bool ready() const {
            return !devid.empty() && !devpassword.empty();
        }
        bool user_ready() const {
            return ready() && !ssid.empty() && !sspassword.empty();
        }
    };
    struct SteamGridDB {
        std::string api_key;
        bool ready() const { return !api_key.empty(); }
    };
    struct RetroAchievements {
        std::string user;
        std::string password;     // plaintext RA web password (preferred for login)
        std::string token;        // pre-obtained Connect API Token (legacy login path)
        std::string web_api_key;  // RA Web API Key — REST stats only (settings → Keys)
        bool ready() const {
            return !user.empty() && (!token.empty() || !password.empty());
        }
        // True when the browser can hit the REST API for pre-play
        // achievement progress on a game the user hasn't booted yet.
        bool rest_ready() const {
            return !user.empty() && !web_api_key.empty();
        }
    };

    ScreenScraper     screenscraper;
    SteamGridDB       steamgriddb;
    RetroAchievements retroachievements;
};

// Reads /foyer/config/accounts.jsonc. Missing keys remain empty. If the file
// doesn't exist this writes a stub the user can fill in, then returns an
// all-empty Accounts.
const Accounts& accounts();

// Force re-read (used after the user edits the file via MTP).
void reload_accounts();

// Generic setter — `path` is "section.key" where section is one of
// "screenscraper", "steamgriddb", "retroachievements". Persists to disk.
void set_account_field(std::string_view path, std::string_view value);

} // namespace foyer::scrapers
