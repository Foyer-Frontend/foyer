#pragma once

#include <borealis.hpp>

namespace foyer::player {

// Lists every shader preset shader_pipeline knows about. Picking
// hot-swaps the active preset via ShaderPipeline::set_preset;
// the next retro_run output runs through the new chain.
class ShadersPickerActivity : public brls::Activity {
public:
    ShadersPickerActivity() = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;
};

}  // namespace foyer::player
