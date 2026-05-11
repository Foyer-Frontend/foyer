#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "libretro.h"

namespace foyer::libretro {

// Foyer's libretro frontend. One instance per process — backed by a hidden
// singleton because libretro's callbacks are pure C function pointers.
//
// Lifecycle (used by the player nro's main()):
//   1. Frontend::instance().init();
//   2. Frontend::instance().load_game(rom_path);
//   3. while (running) Frontend::instance().run_frame();
//   4. Frontend::instance().unload_game();
//   5. Frontend::instance().shutdown();
//
// Aspect handling, post-process shaders, save-state, and the pause
// overlay all sit in their own modules (aspect.hpp, shader.hpp,
// savestate.hpp, overlay.hpp). Frontend stays narrowly focused on the
// libretro contract — env / video / audio / input / log callbacks.
struct Frontend {
    // System metadata pulled from retro_get_system_info() at init time.
    struct SystemInfo {
        std::string library_name;
        std::string library_version;
        std::string valid_extensions;   // "|"-separated, lowercase
        bool        need_fullpath = false;
        bool        block_extract = false;
    };

    // Pixel buffer that the core handed us via retro_video_refresh_t.
    struct VideoFrame {
        const void*       data    = nullptr;
        unsigned          width   = 0;
        unsigned          height  = 0;
        std::size_t       pitch   = 0;
        retro_pixel_format format = RETRO_PIXEL_FORMAT_0RGB1555;
    };

    // Audio buffer the core handed us via retro_audio_sample_batch_t.
    // Format is always interleaved S16 stereo per libretro spec.
    struct AudioFrame {
        const std::int16_t* samples = nullptr; // 2 channels interleaved
        std::size_t         frames  = 0;       // sample-frames (not bytes)
    };

    // Input traits set during init based on the connected pad.
    struct InputState {
        std::uint16_t buttons = 0;     // bitfield indexed by RETRO_DEVICE_ID_JOYPAD_*
        std::int16_t  axes[4] = {0};   // L stick X/Y, R stick X/Y, full-range
    };

    static Frontend& instance();

    // Initialise the core (call once, before load_game).
    bool init();

    // Tear everything down. Safe to call without prior init().
    void shutdown();

    // Load rom from disk. `rom_path` is an sdmc-mounted path. Returns false on
    // any failure (file IO, archive extract, retro_load_game).
    bool load_game(const std::string& rom_path);

    void unload_game();

    // Drive one frame. Pumps input, calls retro_run, and forwards any video
    // / audio output to the platform sinks.
    void run_frame();

    // System metadata, only valid after init().
    const SystemInfo& system_info() const { return m_sys; }

    // Input mirror — populated from libnx pad before each retro_run. The
    // core polls us back via retro_input_state_t to read this.
    InputState& input() { return m_input; }

    // Pixel callback target — owned by platform/video.cpp. The frontend just
    // forwards frames here. Set to nullptr to drop frames.
    using VideoSink = void(*)(const VideoFrame&);
    void set_video_sink(VideoSink sink) { m_video_sink = sink; }

    // Inject a video frame from outside the normal core video_refresh
    // path — used by HwContext to deliver glReadPixels output through
    // the same sink the software path uses.
    void push_video_frame(const VideoFrame& f) {
        if (m_video_sink) m_video_sink(f);
    }

    // Audio callback target — owned by platform/audio.cpp.
    using AudioSink = void(*)(const AudioFrame&);
    void set_audio_sink(AudioSink sink) { m_audio_sink = sink; }

    // Optional logging passthrough. Defaults to stderr/stdout.
    using LogSink = void(*)(retro_log_level, const char*);
    void set_log_sink(LogSink sink) { m_log_sink = sink; }

    // Geometry hints — used by platform/video.cpp to size its output.
    unsigned base_width()  const { return m_av_info.geometry.base_width; }
    unsigned base_height() const { return m_av_info.geometry.base_height; }
    unsigned max_width()   const { return m_av_info.geometry.max_width; }
    unsigned max_height()  const { return m_av_info.geometry.max_height; }
    float    sample_rate() const { return (float)m_av_info.timing.sample_rate; }
    float    fps()         const { return (float)m_av_info.timing.fps; }
    float    aspect_ratio() const { return m_av_info.geometry.aspect_ratio; }

    // Used by retro_environment dispatcher to query the negotiated pixel format.
    retro_pixel_format pixel_format() const { return m_pixel_format; }

    // System directory the core sees via
    // RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY. Most cores happily share
    // /foyer/system/, but PPSSPP libretro reads asset files (atlases,
    // fonts, language inis) directly out of system_dir and would
    // collide with other cores' BIOS files there. The player main.cpp
    // sets this before init() to a per-core path when the core needs
    // its own scoped directory.
    void set_system_directory(std::string path) { m_system_dir = std::move(path); }
    const std::string& system_directory() const { return m_system_dir; }

    // Override the path used to derive the .srm filename. Default
    // is the rom_path passed to load_game(), which may be a
    // temporary extract path for .zip / .7z launches; pointing
    // this at the original sd-side rom keeps SRAM stable across
    // archive vs raw-rom launches of the same game.
    void set_sram_basis_path(std::string path) {
        m_rom_path = std::move(path);
    }

private:
    Frontend() = default;
    Frontend(const Frontend&) = delete;
    Frontend& operator=(const Frontend&) = delete;

    bool                         m_initialised  = false;
    bool                         m_game_loaded  = false;
    SystemInfo                   m_sys{};
    retro_system_av_info         m_av_info{};
    retro_pixel_format           m_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
    std::vector<std::uint8_t>    m_rom_buffer;     // when need_fullpath==false
    std::string                  m_rom_path;       // remembered for SRAM .srm naming
    unsigned                     m_sram_flush_counter = 0;
    InputState                   m_input{};
    std::string                  m_system_dir   = "/foyer/system";

    // Battery-backed memory persistence. retro_get_memory_data(SAVE_RAM)
    // hands us a pointer the core writes in-place; we mirror it to
    // /foyer/saves/<rom_basename>.srm on unload, restore on load. No
    // .srm file => fresh game (the SAVE_RAM region keeps the core's
    // own initial values).
    void load_sram_for(const std::string& rom_path);
    void save_sram_for(const std::string& rom_path);

    VideoSink                    m_video_sink = nullptr;
    AudioSink                    m_audio_sink = nullptr;
    LogSink                      m_log_sink   = nullptr;

    // Static C callbacks the core invokes — declared here so they can touch
    // private state through instance().
    friend bool        env_cb(unsigned, void*);
    friend void        video_refresh_cb(const void*, unsigned, unsigned, std::size_t);
    friend void        audio_sample_cb(std::int16_t, std::int16_t);
    friend std::size_t audio_sample_batch_cb(const std::int16_t*, std::size_t);
    friend void        input_poll_cb();
    friend std::int16_t input_state_cb(unsigned, unsigned, unsigned, unsigned);
    friend void        log_cb(retro_log_level, const char*, ...);
};

} // namespace foyer::libretro
