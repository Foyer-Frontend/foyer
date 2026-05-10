#pragma once

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>

#include <functional>
#include <string>

namespace foyer::browser {

// brls::InputCell renders the stored value verbatim in the cell's
// detail label, which is the wrong shape for password / API-key
// fields — the user can read the secret over their shoulder. This
// subclass keeps the same init() shape but masks the detail with
// bullets ("••••••••") whenever a value is set, while still
// firing the original-value callback to the consumer.
//
// We subclass DetailCell directly rather than InputCell because
// InputCell::updateUI is private (rewrites the detail label
// unconditionally) and there's no virtual hook to override.
class MaskedInputCell : public brls::DetailCell {
public:
    MaskedInputCell();

    // Mirrors brls::InputCell::init.
    void init(std::string title,
              std::string value,
              std::function<void(std::string)> on_change,
              std::string placeholder = "",
              std::string hint = "",
              int max_input_length = 64);

private:
    std::string m_value;
    std::string m_placeholder;
    std::string m_hint;
    int         m_max_input_length = 64;

    std::function<void(std::string)> m_on_change;

    void refresh_detail();
};

} // namespace foyer::browser
