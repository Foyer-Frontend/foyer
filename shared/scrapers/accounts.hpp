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
//     "retroachievements": { "user":  "", "token":     "" }
//   }
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
        std::string token;
        bool ready() const { return !user.empty() && !token.empty(); }
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
