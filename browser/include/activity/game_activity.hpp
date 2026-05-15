#pragma once

#include <borealis.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::browser {

// Detail view for one game.
//
// Layout (per game.xml):
//   - fanart.jpg full-screen backdrop
//   - top bar: title (left) + clock/wifi/battery (right)
//   - body: sstitle + ss screenshots stacked left,
//           meta panel (publisher / developer / players / rating /
//           genre / release date / synopsis) on the right
//   - bottom hint bar
//
// Asset bundle path: /foyer/assets/system/<sys>/<stem>/. Files
// resolved via cache::find_in_dir to handle the SS region tag
// (box-2D(us).png / sstitle(eu).png / …).
class GameActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/game.xml");

    GameActivity(std::string_view system_folder, std::string_view game_path);
    ~GameActivity() override;

    void onContentAvailable() override;

    BRLS_BIND(brls::Label,  clock,        "foyer/clock");
    BRLS_BIND(brls::Image,  fanart,       "foyer/game_fanart");
    BRLS_BIND(brls::Box,    galleryHolder,"foyer/game_gallery_holder");
    BRLS_BIND(brls::Image,  slide,        "foyer/game_slide");
    BRLS_BIND(brls::Label,  slideCaption, "foyer/game_slide_caption");
    BRLS_BIND(brls::Label,  gameTitle,    "foyer/game_title");
    BRLS_BIND(brls::Image,  gameWheel,    "foyer/game_wheel");
    BRLS_BIND(brls::Box,    metaHolder,   "foyer/game_meta");
    BRLS_BIND(brls::Box,    body,         "foyer/game_body");

private:
    std::string m_system_folder;
    std::string m_game_path;
    std::string m_game_stem;

    brls::RepeatingTask* m_clockTask = nullptr;

    // Slide-show state. Paths into the per-game asset bundle, in
    // display order (sstitle first, then ss, then anything else
    // the scraper saved). Index wraps with up/down on the
    // gallery holder.
    std::vector<std::string> m_slides;
    int                      m_slide_idx = 0;

    void buildMetaPanel();
    void buildGallery();
    void rebuildGalleryContent();
    void show_slide(int idx);

public:
    // Called from the rescrape worker's completion lambda (via
    // brls::sync) so the live game-details view picks up the
    // freshly downloaded metadata + screenshots WITHOUT needing
    // the user to back out + re-enter. Re-reads metadata.json,
    // rewrites the title / fanart / meta panel, and rescans the
    // bundle dir for new screenshots. Gallery action handlers are
    // registered exactly once in onContentAvailable so a refresh
    // doesn't stack duplicate UP/DOWN bindings.
    void refresh_from_disk();
};

}  // namespace foyer::browser
