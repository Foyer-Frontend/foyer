#include "widgets/masked_input_cell.hpp"

namespace foyer::browser {

namespace {

std::string mask_for(const std::string& value) {
    if (value.empty()) return std::string();
    // Fixed-width mask hides the actual secret length too, which
    // is a small extra safeguard versus matching len-by-len.
    return "••••••••";
}

}  // namespace

MaskedInputCell::MaskedInputCell() {
    detail->setTextColor(
        brls::Application::getTheme()["brls/list/listItem_value_color"]);

    this->registerClickAction([this](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                m_value = text;
                if (m_on_change) m_on_change(m_value);
                refresh_detail();
            },
            this->title->getFullText(),
            m_hint,
            m_max_input_length,
            m_value,
            0 /* no keyboard restrictions */);
        return true;
    });
}

void MaskedInputCell::init(std::string title_text,
                           std::string value,
                           std::function<void(std::string)> on_change,
                           std::string placeholder,
                           std::string hint,
                           int max_input_length)
{
    this->title->setText(title_text);
    m_value             = std::move(value);
    m_placeholder       = std::move(placeholder);
    m_hint              = std::move(hint);
    m_max_input_length  = max_input_length;
    m_on_change         = std::move(on_change);
    refresh_detail();
}

void MaskedInputCell::refresh_detail() {
    auto theme = brls::Application::getTheme();
    if (m_value.empty()) {
        detail->setText(m_placeholder);
        detail->setTextColor(theme["brls/text_disabled"]);
    } else {
        detail->setText(mask_for(m_value));
        detail->setTextColor(theme["brls/list/listItem_value_color"]);
    }
}

} // namespace foyer::browser
