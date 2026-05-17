#include "imgui/imgui_theme.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"

#include <imgui.h>
#include <switch.h>

namespace foyer::player::imgui_shell {

namespace { int g_last = -1; }

bool theme_want_dark() {
    const auto& ov = ::foyer::library::config().theme_override;
    if (ov == "light") return false;
    if (ov == "dark")  return true;
    ColorSetId id;
    if (R_FAILED(setsysGetColorSetId(&id))) return true;
    return id == ColorSetId_Dark;
}

void theme_apply() {
    const bool dark = theme_want_dark();
    if (dark) ImGui::StyleColorsDark();
    else      ImGui::StyleColorsLight();
    g_last = dark ? 1 : 0;
    foyer::log::write("[imgui_theme] applied %s\n", dark ? "dark" : "light");
}

bool theme_apply_if_changed() {
    const int now = theme_want_dark() ? 1 : 0;
    if (now == g_last) return false;
    theme_apply();
    return true;
}

}  // namespace foyer::player::imgui_shell
