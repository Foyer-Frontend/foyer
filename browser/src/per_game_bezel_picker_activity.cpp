#include "activity/per_game_bezel_picker_activity.hpp"

#include "library/bezel_installer.hpp"
#include "library/config.hpp"
#include "library/system_db.hpp"
#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>

#include <algorithm>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

using namespace brls::literals;

namespace foyer::browser {
namespace {

constexpr const char* kPerSystemRoot = "/foyer/content/bezels/";

bool file_exists(const std::string& p) {
    if (p.empty()) return false;
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

std::string bundle_dir(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/system/"}
         + std::string{sys} + "/" + std::string{stem} + "/";
}

std::vector<std::string> scan_bundle_bezels(std::string_view sys,
                                            std::string_view stem) {
    std::vector<std::string> out;
    const auto dir = bundle_dir(sys, stem);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (auto* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string n = e->d_name;
        // Match anything beginning with "bezel" and ending in .png —
        // covers SS region-tagged scrapes (bezel-16-9(us).png), bare
        // bezel.png, and source-tagged downloads (bezel-bezelproject.png,
        // bezel-estefan.png).
        if (n.rfind("bezel", 0) != 0) continue;
        if (n.size() <= 4 || n.compare(n.size() - 4, 4, ".png") != 0) continue;
        out.push_back(dir + n);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::string label_for_bundle_file(const std::string& full_path) {
    auto slash = full_path.find_last_of('/');
    std::string base = slash == std::string::npos
        ? full_path
        : full_path.substr(slash + 1);
    if (base.size() > 4) base.resize(base.size() - 4);  // drop .png
    return "This game — " + base;
}

}  // namespace

PerGameBezelPickerActivity::PerGameBezelPickerActivity(
        std::string rom_path,
        std::string system_folder,
        std::string rom_stem,
        std::string current_choice,
        CommitFn on_commit)
    : m_rom_path(std::move(rom_path)),
      m_system_folder(std::move(system_folder)),
      m_rom_stem(std::move(rom_stem)),
      m_current_choice(std::move(current_choice)),
      m_on_commit(std::move(on_commit)) {}

void PerGameBezelPickerActivity::build_entries() {
    m_entries.clear();
    m_entries.push_back({ "(auto)", "" });

    // Per-game bezels in the rom's bundle dir come first — these
    // are the ones SS scraped or that the user downloaded via the
    // online-bezel browser. They're rom-specific, so they head the
    // list above the broader per-system picks.
    for (const auto& path : scan_bundle_bezels(m_system_folder, m_rom_stem)) {
        m_entries.push_back({ label_for_bundle_file(path), path });
    }

    // Per-system installed packs filtered to the rom's family —
    // same shape as PerSystemActivity's picker. With the multi-
    // source catalogue an unfiltered list mixes every system's
    // bezels into every rom's picker; family filter keeps it
    // relevant.
    const std::string folder = m_system_folder;
    const auto fam = std::string{
        ::foyer::library::family_for_folder(folder)};
    auto matches = [&](const std::string& n, const std::string& key) {
        if (n == key) return true;
        if (n.size() > key.size() + 1
            && n.compare(0, key.size(), key) == 0
            && n[key.size()] == '-') return true;
        return false;
    };
    for (const auto& name : ::foyer::library::installed_bezel_names()) {
        if (matches(name, folder)
            || (fam != folder && matches(name, fam))) {
            m_entries.push_back({
                name,
                std::string{kPerSystemRoot} + name + ".png" });
        }
    }
}

brls::View* PerGameBezelPickerActivity::createContentView() {
    build_entries();

    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setGrow(1.0f);
    root->setBackgroundColor(brls::Application::getTheme().getColor(
        "brls/background"));

    auto* title = new brls::Label();
    title->setText("Default bezel for this game");
    title->setFontSize(28.0f);
    title->setMargins(20.0f, 0.0f, 12.0f, 0.0f);
    title->setTextColor(brls::Application::getTheme().getColor(
        "brls/text"));
    root->addView(title);

    m_preview = new brls::Image();
    m_preview->setWidth(960.0f);
    m_preview->setHeight(540.0f);
    m_preview->setScalingType(brls::ImageScalingType::FIT);
    m_preview->setMargins(12.0f, 0.0f, 12.0f, 0.0f);
    root->addView(m_preview);

    auto* meta = new brls::Box();
    meta->setAxis(brls::Axis::ROW);
    meta->setAlignItems(brls::AlignItems::CENTER);
    meta->setJustifyContent(brls::JustifyContent::CENTER);
    meta->setMargins(8.0f, 0.0f, 8.0f, 0.0f);

    m_label = new brls::Label();
    m_label->setFontSize(22.0f);
    m_label->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    m_label->setTextColor(brls::Application::getTheme().getColor(
        "brls/text"));
    meta->addView(m_label);

    m_index_lbl = new brls::Label();
    m_index_lbl->setFontSize(18.0f);
    m_index_lbl->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    meta->addView(m_index_lbl);
    root->addView(meta);

    auto* hint = new brls::Label();
    hint->setText("←/→ to switch · A to confirm · B to cancel");
    hint->setFontSize(16.0f);
    hint->setMargins(16.0f, 0.0f, 24.0f, 0.0f);
    hint->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    root->addView(hint);

    // Seed the focused index. Priority:
    //   1. The user's existing explicit pick (current_choice) wins
    //      verbatim — let them edit a prior choice without losing
    //      cursor position.
    //   2. Otherwise mirror the resolver's effective default — if
    //      "Always use system bezel" is on for this system or its
    //      hardware family, land on the first per-system entry;
    //      else if a game-specific bezel exists, land on the first
    //      "This game" entry. Falls through to "(auto)" if neither
    //      applies.
    m_idx = 0;
    bool seeded = false;
    if (!m_current_choice.empty()) {
        for (std::size_t i = 0; i < m_entries.size(); i++) {
            if (m_entries[i].path == m_current_choice) {
                m_idx = (int)i;
                seeded = true;
                break;
            }
        }
    }
    if (!seeded) {
        const auto fam = std::string{
            ::foyer::library::family_for_folder(m_system_folder)};
        const auto& cfg = ::foyer::library::config();
        const bool force =
            cfg.is_force_default_bezel_for(m_system_folder)
            || (fam != m_system_folder
                && cfg.is_force_default_bezel_for(fam));
        auto pick_first_with = [&](const std::string& prefix) -> int {
            for (std::size_t i = 0; i < m_entries.size(); i++) {
                if (m_entries[i].path.rfind(prefix, 0) == 0) {
                    return (int)i;
                }
            }
            return -1;
        };
        // System packs live under /foyer/content/bezels/, per-game
        // files under /foyer/assets/system/.
        const int sys_idx  = pick_first_with(kPerSystemRoot);
        const int game_idx = pick_first_with("/foyer/assets/system/");
        if (force && sys_idx >= 0)             m_idx = sys_idx;
        else if (!force && game_idx >= 0)      m_idx = game_idx;
        else if (sys_idx >= 0)                 m_idx = sys_idx;
        else                                   m_idx = 0;
    }

    return root;
}

void PerGameBezelPickerActivity::onContentAvailable() {
    show_index(m_idx);

    auto* cv = this->getContentView();
    if (!cv) return;
    cv->setFocusable(true);

    cv->registerAction("Prev", brls::BUTTON_LEFT,
        [this](brls::View*) {
            if (m_entries.empty()) return false;
            m_idx = (m_idx - 1 + (int)m_entries.size()) % (int)m_entries.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Next", brls::BUTTON_RIGHT,
        [this](brls::View*) {
            if (m_entries.empty()) return false;
            m_idx = (m_idx + 1) % (int)m_entries.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Confirm", brls::BUTTON_A,
        [this](brls::View*) {
            if (m_entries.empty()) return false;
            if (m_on_commit) m_on_commit(m_entries[m_idx].path);
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_CLICK);

    cv->registerAction("Cancel", brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    brls::Application::giveFocus(cv);
}

void PerGameBezelPickerActivity::show_index(int idx) {
    if (m_entries.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= (int)m_entries.size()) idx = (int)m_entries.size() - 1;
    m_idx = idx;

    const auto& e = m_entries[idx];
    if (m_label) m_label->setText(e.label);
    if (m_index_lbl) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "   %d / %d",
            idx + 1, (int)m_entries.size());
        m_index_lbl->setText(buf);
    }
    if (m_preview) {
        if (file_exists(e.path)) {
            m_preview->setImageFromFile(e.path);
        } else {
            m_preview->clear();
        }
    }
}

}  // namespace foyer::browser
