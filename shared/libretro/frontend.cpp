#include "frontend.hpp"
#include "core_options.hpp"
#include "platform/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

// libretro core symbols. Declared `extern` because the core is statically
// linked into the same binary as the frontend.
extern "C" {
    void retro_init(void);
    void retro_deinit(void);
    void retro_set_environment(retro_environment_t);
    void retro_set_video_refresh(retro_video_refresh_t);
    void retro_set_audio_sample(retro_audio_sample_t);
    void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
    void retro_set_input_poll(retro_input_poll_t);
    void retro_set_input_state(retro_input_state_t);
    void retro_get_system_info(struct retro_system_info*);
    void retro_get_system_av_info(struct retro_system_av_info*);
    void retro_set_controller_port_device(unsigned, unsigned);
    void retro_reset(void);
    void retro_run(void);
    bool retro_load_game(const struct retro_game_info*);
    void retro_unload_game(void);
    unsigned retro_get_region(void);
    unsigned retro_api_version(void);
}

namespace foyer::libretro {

// Forward decls so the env_cb dispatcher can reference log_cb before its
// definition further down.
bool        env_cb(unsigned, void*);
void        video_refresh_cb(const void*, unsigned, unsigned, std::size_t);
void        audio_sample_cb(std::int16_t, std::int16_t);
std::size_t audio_sample_batch_cb(const std::int16_t*, std::size_t);
void        input_poll_cb();
std::int16_t input_state_cb(unsigned, unsigned, unsigned, unsigned);
void        log_cb(retro_log_level, const char*, ...);

// ---------------------------------------------------------------------------
// Static callback dispatch — these are the function pointers we register on
// the core. Each one looks up the singleton and forwards.
// ---------------------------------------------------------------------------

bool env_cb(unsigned cmd, void* data) {
    auto& fe = Frontend::instance();
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const auto fmt = *static_cast<retro_pixel_format*>(data);
            // Accept the three standard formats. fceumm asks for XRGB8888.
            if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 &&
                fmt != RETRO_PIXEL_FORMAT_XRGB8888 &&
                fmt != RETRO_PIXEL_FORMAT_RGB565) {
                return false;
            }
            const_cast<retro_pixel_format&>(fe.m_pixel_format) = fmt;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            auto* iface = static_cast<retro_log_callback*>(data);
            iface->log = log_cb;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
            *static_cast<bool*>(data) = true;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* var = static_cast<retro_variable*>(data);
            var->value = CoreOptions::instance().get(var->key);
            return var->value != nullptr;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            *static_cast<bool*>(data) = CoreOptions::instance().consume_dirty();
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            CoreOptions::instance().ingest_legacy(
                static_cast<const retro_variable*>(data));
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
            CoreOptions::instance().ingest_v1(
                static_cast<const retro_core_option_definition*>(data));
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            // Use the english (default) definitions; ignore the intl table.
            auto* intl = static_cast<const retro_core_options_intl*>(data);
            if (intl) CoreOptions::instance().ingest_v1(intl->us);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            CoreOptions::instance().ingest_v2(
                static_cast<const retro_core_options_v2*>(data));
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            auto* intl = static_cast<const retro_core_options_v2_intl*>(data);
            if (intl) CoreOptions::instance().ingest_v2(intl->us);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
            // We don't honour visibility hints yet — show every option.
            return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            // BIOS/firmware/system files for the core.
            static const char* path = "/foyer/system";
            *static_cast<const char**>(data) = path;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            static const char* path = "/foyer/saves";
            *static_cast<const char**>(data) = path;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;
        default:
            return false;
    }
}

void video_refresh_cb(const void* data, unsigned w, unsigned h, std::size_t pitch) {
    auto& fe = Frontend::instance();
    if (!fe.m_video_sink || !data) return;
    Frontend::VideoFrame frame{ data, w, h, pitch, fe.m_pixel_format };
    fe.m_video_sink(frame);
}

void audio_sample_cb(std::int16_t l, std::int16_t r) {
    auto& fe = Frontend::instance();
    if (!fe.m_audio_sink) return;
    const std::int16_t pair[2] = { l, r };
    Frontend::AudioFrame frame{ pair, 1 };
    fe.m_audio_sink(frame);
}

std::size_t audio_sample_batch_cb(const std::int16_t* data, std::size_t frames) {
    auto& fe = Frontend::instance();
    if (!fe.m_audio_sink) return frames;
    Frontend::AudioFrame frame{ data, frames };
    fe.m_audio_sink(frame);
    return frames;
}

void input_poll_cb() {
    // Polling already happened on the platform side before run_frame() was
    // called; this is a no-op so the core can call it freely.
}

std::int16_t input_state_cb(unsigned port, unsigned device, unsigned /*index*/, unsigned id) {
    auto& fe = Frontend::instance();
    if (port != 0) return 0;
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id < 16) {
            return (fe.m_input.buttons >> id) & 1u;
        }
    } else if (device == RETRO_DEVICE_ANALOG) {
        const unsigned axis_idx = id;     // 0..3
        if (axis_idx < 4) return fe.m_input.axes[axis_idx];
    }
    return 0;
}

void log_cb(retro_log_level level, const char* fmt, ...) {
    char buf[1024];
    std::va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    auto& fe = Frontend::instance();
    if (fe.m_log_sink) {
        fe.m_log_sink(level, buf);
    } else {
        foyer::log::write("[core] %s", buf);
    }
}

// ---------------------------------------------------------------------------
// Frontend implementation
// ---------------------------------------------------------------------------

Frontend& Frontend::instance() {
    static Frontend g;
    return g;
}

bool Frontend::init() {
    if (m_initialised) return true;

    // CoreOptions needs the core name before SET_VARIABLES fires so the
    // overrides JSONC at /foyer/config/cores/<name>.jsonc resolves.
#if defined(FOYER_CORE_NAME)
#  define FOYER_STR2(x) #x
#  define FOYER_STR(x)  FOYER_STR2(x)
    CoreOptions::instance().set_core_name(FOYER_STR(FOYER_CORE_NAME));
#  undef FOYER_STR
#  undef FOYER_STR2
#endif

    // Wire callbacks before retro_init so the core sees them during its own
    // init sequence (some cores call env from inside retro_init).
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_refresh_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_sample_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();

    retro_system_info info{};
    retro_get_system_info(&info);
    if (info.library_name)     m_sys.library_name     = info.library_name;
    if (info.library_version)  m_sys.library_version  = info.library_version;
    if (info.valid_extensions) m_sys.valid_extensions = info.valid_extensions;
    m_sys.need_fullpath = info.need_fullpath;
    m_sys.block_extract = info.block_extract;

    foyer::log::write("[core] %s %s (api %u, fullpath=%d)\n",
        m_sys.library_name.c_str(),
        m_sys.library_version.c_str(),
        retro_api_version(),
        (int)m_sys.need_fullpath);

    // NOTE: don't call retro_set_controller_port_device() here. Some cores
    // (fceumm) deref internal state that only exists after retro_load_game(),
    // so doing it pre-load null-derefs. Cores default port 0 to JOYPAD; the
    // pause overlay will let the user override later.

    m_initialised = true;
    return true;
}

void Frontend::shutdown() {
    if (m_game_loaded) unload_game();
    if (m_initialised) retro_deinit();
    m_initialised = false;
}

bool Frontend::load_game(const std::string& rom_path) {
    if (!m_initialised) return false;
    if (m_game_loaded) unload_game();

    retro_game_info gi{};
    gi.path = rom_path.c_str();

    if (!m_sys.need_fullpath) {
        // Slurp into RAM; the core wants the data buffer.
        std::ifstream in{rom_path, std::ios::binary};
        if (!in) {
            foyer::log::write("[load] open failed: %s\n", rom_path.c_str());
            return false;
        }
        in.seekg(0, std::ios::end);
        const auto size = (std::size_t)in.tellg();
        in.seekg(0, std::ios::beg);
        m_rom_buffer.assign(size, 0);
        if (!in.read(reinterpret_cast<char*>(m_rom_buffer.data()), (std::streamsize)size)) {
            foyer::log::write("[load] read failed: %s\n", rom_path.c_str());
            return false;
        }
        gi.data = m_rom_buffer.data();
        gi.size = m_rom_buffer.size();
    }

    if (!retro_load_game(&gi)) {
        foyer::log::write("[load] retro_load_game refused %s\n", rom_path.c_str());
        m_rom_buffer.clear();
        m_rom_buffer.shrink_to_fit();
        return false;
    }

    retro_get_system_av_info(&m_av_info);
    foyer::log::write("[av] base=%ux%u max=%ux%u sample=%g fps=%g\n",
        m_av_info.geometry.base_width,
        m_av_info.geometry.base_height,
        m_av_info.geometry.max_width,
        m_av_info.geometry.max_height,
        m_av_info.timing.sample_rate,
        m_av_info.timing.fps);

    m_game_loaded = true;
    return true;
}

void Frontend::unload_game() {
    if (!m_game_loaded) return;
    retro_unload_game();
    m_rom_buffer.clear();
    m_rom_buffer.shrink_to_fit();
    m_game_loaded = false;
}

void Frontend::run_frame() {
    if (!m_game_loaded) return;
    retro_run();
}

} // namespace foyer::libretro
