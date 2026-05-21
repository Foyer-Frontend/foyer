#include "activity/bezel_picker_activity.hpp"

#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>

#include <sys/stat.h>

using namespace brls::literals;

namespace foyer::browser {

namespace {

constexpr const char* kBezelDir = "/foyer/content/bezels/";

std::string bezel_path_for(const std::string& name) {
    if (name.empty()) return {};
    return std::string{kBezelDir} + name + ".png";
}

bool file_exists(const std::string& p) {
    if (p.empty()) return false;
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

BezelPickerActivity::BezelPickerActivity(std::vector<std::string> names,
                                         std::string initial_pick,
                                         ConfirmFn on_confirm)
    : m_names(std::move(names)),
      m_on_confirm(std::move(on_confirm))
{
    if (m_names.empty() || !m_names.front().empty()) {
        // Ensure index 0 is always the "(none)" sentinel for a
        // uniform indexing rule. Callers should pass the sentinel
        // first; this is just defensive.
        m_names.insert(m_names.begin(), std::string{});
    }
    // Seed the focused index from initial_pick. Empty string =
    // "(none)" = index 0.
    m_idx = 0;
    for (std::size_t i = 0; i < m_names.size(); i++) {
        if (m_names[i] == initial_pick) { m_idx = (int)i; break; }
    }
}

brls::View* BezelPickerActivity::createContentView() {
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setGrow(1.0f);
    root->setBackgroundColor(brls::Application::getTheme().getColor(
        "brls/background"));

    // Title row.
    auto* title = new brls::Label();
    title->setText("Default bezel");
    title->setFontSize(28.0f);
    title->setMargins(20.0f, 0.0f, 12.0f, 0.0f);
    title->setTextColor(brls::Application::getTheme().getColor(
        "brls/text"));
    root->addView(title);

    // Big preview image — fills most of the available height.
    // Fixed 16:9 box at 960×540 leaves room above for the title
    // and below for the name + nav hints; fits comfortably in
    // both 1280×720 (handheld) and 1920×1080 (docked).
    m_preview = new brls::Image();
    m_preview->setWidth(960.0f);
    m_preview->setHeight(540.0f);
    m_preview->setScalingType(brls::ImageScalingType::FIT);
    m_preview->setMargins(12.0f, 0.0f, 12.0f, 0.0f);
    root->addView(m_preview);

    // Name + index pair under the preview.
    auto* meta = new brls::Box();
    meta->setAxis(brls::Axis::ROW);
    meta->setAlignItems(brls::AlignItems::CENTER);
    meta->setJustifyContent(brls::JustifyContent::CENTER);
    meta->setMargins(8.0f, 0.0f, 8.0f, 0.0f);

    m_name_label = new brls::Label();
    m_name_label->setFontSize(22.0f);
    m_name_label->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    m_name_label->setTextColor(brls::Application::getTheme().getColor(
        "brls/text"));
    meta->addView(m_name_label);

    m_index_label = new brls::Label();
    m_index_label->setFontSize(18.0f);
    m_index_label->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    meta->addView(m_index_label);
    root->addView(meta);

    // Nav hint label.
    auto* hint = new brls::Label();
    hint->setText("←/→ to switch · A to confirm · B to cancel");
    hint->setFontSize(16.0f);
    hint->setMargins(16.0f, 0.0f, 24.0f, 0.0f);
    hint->setTextColor(brls::Application::getTheme().getColor(
        "brls/text_disabled"));
    root->addView(hint);

    return root;
}

void BezelPickerActivity::onContentAvailable() {
    show_index(m_idx);

    auto* cv = this->getContentView();
    if (!cv) return;
    cv->setFocusable(true);

    cv->registerAction("Prev", brls::BUTTON_LEFT,
        [this](brls::View*) {
            if (m_names.empty()) return false;
            m_idx = (m_idx - 1 + (int)m_names.size()) % (int)m_names.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Next", brls::BUTTON_RIGHT,
        [this](brls::View*) {
            if (m_names.empty()) return false;
            m_idx = (m_idx + 1) % (int)m_names.size();
            show_index(m_idx);
            return true;
        }, /*hidden=*/true, /*allowRepeating=*/true,
           brls::SOUND_FOCUS_CHANGE);

    cv->registerAction("Confirm", brls::BUTTON_A,
        [this](brls::View*) {
            if (m_on_confirm) m_on_confirm(m_names[m_idx]);
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

void BezelPickerActivity::show_index(int idx) {
    if (m_names.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= (int)m_names.size()) idx = (int)m_names.size() - 1;
    m_idx = idx;

    const auto& name = m_names[idx];
    if (m_name_label) {
        m_name_label->setText(name.empty() ? "(none)" : name);
    }
    if (m_index_label) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "   %d / %d",
            idx + 1, (int)m_names.size());
        m_index_label->setText(buf);
    }
    if (m_preview) {
        const auto p = bezel_path_for(name);
        if (file_exists(p)) {
            m_preview->setImageFromFile(p);
        } else {
            m_preview->clear();
        }
    }
}

}  // namespace foyer::browser
