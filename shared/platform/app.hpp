#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <switch.h>

// Forward decls so this header doesn't drag in deko3d / nanovg headers.
struct NVGcontext;

namespace nvg { class DkRenderer; }
namespace dk  {
    class CCmdMemRing;
    class CMemPool;
}

namespace foyer::platform {

// One-process bring-up of graphics + audio + input.
//
// This class is the "root" of any foyer binary (browser or player). It owns
// the deko3d device, the framebuffer chain, the nanovg-deko3d renderer, and
// the persistent romfs mount that works around libnx 4.12's leaky
// romfsInit/Exit cycles.
//
// Lifetime: construct exactly once at the start of main(). Destruct only on
// process exit.
struct App {
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Drives one frame of input + draw. Returns false when the user has
    // requested exit (HOME button under applet mode, etc.).
    bool tick();

    // Imperative quit — sets the loop to bail at next tick boundary.
    void quit() { m_quit = true; }

    // Optional per-frame draw hook. Called between framebuffer clear and
    // submit, with the active nanovg context already inside a frame. The
    // browser/player nro overrides the default placeholder text by setting
    // this. Set to nullptr to restore the default.
    using DrawFn = std::function<void(NVGcontext*, float, float)>;
    void set_draw_fn(DrawFn fn) { m_draw_fn = std::move(fn); }

    // Pad accessor — caller can sample buttons / sticks before tick(). The
    // App polls in tick() too, but exposing this lets the player's libretro
    // input layer mirror state without polling twice.
    PadState& pad() { return m_pad; }

    // Touch screen state for the just-completed frame. UI code reads this
    // and dispatches taps to its hit-tested elements. `points[i]` is valid
    // for i < count; positions are in framebuffer pixels.
    struct Touch {
        int  count = 0;
        struct Point {
            float x = 0;
            float y = 0;
            int   id = -1;
        };
        Point points[4]{};
        // True on the FRAME a new finger first touched the screen.
        bool tap_started = false;
    };
    const Touch& touch() const { return m_touch; }

    // Accessors used by UI code.
    NVGcontext* vg()         const { return m_vg; }
    int         width()      const { return m_fb_w; }
    int         height()     const { return m_fb_h; }
    float       scale()      const { return m_scale; }

private:
    // 16:9 logical UI canvas (1280×720, same convention as the brls
    // demo + RetroArch's overlay coordinate space).
    static constexpr int kLogicalW = 1280;
    static constexpr int kLogicalH = 720;

    void init_fs();
    void init_gfx();
    void init_input();
    void exit_gfx();

    // Graphics state — opaque to outside callers. Stored as void* so the
    // deko3d C++ types only need to be visible to app.cpp.
    void*           m_device_storage   = nullptr;
    void*           m_queue_storage    = nullptr;
    void*           m_pool_images      = nullptr;
    void*           m_pool_code        = nullptr;
    void*           m_pool_data        = nullptr;
    void*           m_cmdbuf_storage   = nullptr;
    void*           m_swapchain        = nullptr;
    void*           m_renderer_storage = nullptr;
    NVGcontext*     m_vg               = nullptr;

    PadState        m_pad{};
    Touch           m_touch{};
    int             m_prev_touch_count = 0;

    int             m_fb_w   = kLogicalW;
    int             m_fb_h   = kLogicalH;
    float           m_scale  = 1.0f;

    bool            m_romfs_mounted = false;
    bool            m_quit          = false;

    DrawFn          m_draw_fn{};
};

} // namespace foyer::platform
