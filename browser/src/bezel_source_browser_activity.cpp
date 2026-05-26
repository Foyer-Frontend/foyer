#include "activity/bezel_source_browser_activity.hpp"

#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>

#include <thread>

using namespace brls::literals;

namespace foyer::browser {

BezelSourceBrowserActivity::BezelSourceBrowserActivity(
        std::string system_folder,
        std::string rom_stem,
        CommitFn on_commit)
    : m_system_folder(std::move(system_folder)),
      m_rom_stem(std::move(rom_stem)),
      m_on_commit(std::move(on_commit)) {}

BezelSourceBrowserActivity::~BezelSourceBrowserActivity() {
    // Clean up temp previews when the activity tears down.
    ::foyer::library::clear_per_game_bezel_preview_cache(
        m_system_folder, m_rom_stem);
}

brls::View* BezelSourceBrowserActivity::createContentView() {
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setGrow(1.0f);
    root->setBackgroundColor(brls::Application::getTheme().getColor(
        "brls/background"));

    auto* title = new brls::Label();
    title->setText("Online bezels");
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

    m_source_lbl = new brls::Label();
    m_source_lbl->setFontSize(22.0f);
    m_source_lbl->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    m_source_lbl->setTextColor(brls::Application::getTheme().getColor(
        "brls/text"));
    meta->addView(m_source_lbl);

    m_index_lbl = new brls::Label();
    m_index_lbl->setFontSize(18.0f);
    m_index_lbl->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    meta->addView(m_index_lbl);
    root->addView(meta);

    m_status = new brls::Label();
    m_status->setText("Searching The Bezel Project + Realistic…");
    m_status->setFontSize(18.0f);
    m_status->setMargins(8.0f, 0.0f, 4.0f, 0.0f);
    m_status->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    root->addView(m_status);

    m_hint_lbl = new brls::Label();
    m_hint_lbl->setText("");
    m_hint_lbl->setFontSize(16.0f);
    m_hint_lbl->setMargins(16.0f, 0.0f, 24.0f, 0.0f);
    m_hint_lbl->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    root->addView(m_hint_lbl);

    return root;
}

void BezelSourceBrowserActivity::onContentAvailable() {
    auto* cv = this->getContentView();
    if (!cv) return;
    cv->setFocusable(true);

    cv->registerAction("Prev", brls::BUTTON_LEFT,
        [this](brls::View*) {
            if (m_hits.empty()) return false;
            m_idx = (m_idx - 1 + (int)m_hits.size()) % (int)m_hits.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Next", brls::BUTTON_RIGHT,
        [this](brls::View*) {
            if (m_hits.empty()) return false;
            m_idx = (m_idx + 1) % (int)m_hits.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Save", brls::BUTTON_A,
        [this](brls::View*) {
            if (m_hits.empty()) return false;
            const auto& h = m_hits[m_idx];
            const bool ok = ::foyer::library::commit_bezel_to_bundle(
                m_system_folder, m_rom_stem, h.source, h.temp_path);
            if (ok && m_on_commit) m_on_commit(h.source);
            if (m_status) m_status->setText(
                ok ? "Saved." : "Save failed.");
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_CLICK);

    cv->registerAction("Cancel", brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    brls::Application::giveFocus(cv);

    // Kick the network probe synchronously on the UI thread —
    // foyer::net's pump callback keeps brls painting while curl
    // blocks. Probe is short (4-6 HTTPS round-trips on cache miss).
    kick_search();
}

void BezelSourceBrowserActivity::kick_search() {
    const std::string sys = m_system_folder;
    const std::string stem = m_rom_stem;
    auto progress = [this](const ::foyer::library::PerGameBezelProgress& p) {
        if (!m_status) return;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[%d/%d] %s",
            p.step, p.total, p.detail.c_str());
        m_status->setText(buf);
    };
    auto hits = ::foyer::library::peek_per_game_bezels(sys, stem, progress);
    on_search_done(std::move(hits));
}

void BezelSourceBrowserActivity::on_search_done(
        std::vector<::foyer::library::PerGameBezelPreview> hits) {
    m_searching = false;
    m_hits = std::move(hits);
    if (m_hits.empty()) {
        if (m_status) m_status->setText(
            "No online bezels found for this rom.");
        if (m_hint_lbl) m_hint_lbl->setText("B to go back");
        if (m_source_lbl) m_source_lbl->setText("");
        if (m_index_lbl) m_index_lbl->setText("");
        if (m_preview) m_preview->clear();
        return;
    }
    if (m_status) m_status->setText("");
    if (m_hint_lbl) m_hint_lbl->setText(
        "←/→ switch source · A save · B cancel");
    m_idx = 0;
    show_index(0);
}

void BezelSourceBrowserActivity::show_index(int idx) {
    if (m_hits.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= (int)m_hits.size()) idx = (int)m_hits.size() - 1;
    m_idx = idx;
    const auto& h = m_hits[idx];
    if (m_source_lbl) m_source_lbl->setText(h.label);
    if (m_index_lbl) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "   %d / %d",
            idx + 1, (int)m_hits.size());
        m_index_lbl->setText(buf);
    }
    if (m_preview) m_preview->setImageFromFile(h.temp_path);
}

}  // namespace foyer::browser
