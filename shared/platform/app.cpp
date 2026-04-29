#include "app.hpp"
#include "log.hpp"

#include <array>
#include <cstring>
#include <new>

#include <deko3d.hpp>
#include <nanovg.h>
#include <nanovg_dk.h>
#include <nanovg/dk_renderer.hpp>

namespace foyer::platform {
namespace {

constexpr unsigned kFramebufferCount  = 2;
constexpr unsigned kStaticCmdSize     = 0x10000;

// All deko3d wrappers and CMemPool need a constructed device before they can
// be built. We use UniqueXxx (deko3d's unique_ptr-ish wrappers) and
// std::optional<CMemPool> / std::optional<nvg::DkRenderer> so the heavy
// objects can be constructed lazily inside init_gfx().
struct Gfx {
    dk::UniqueDevice              device;
    dk::UniqueQueue               queue;
    dk::UniqueSwapchain           swapchain;
    dk::UniqueCmdBuf              cmdbuf;
    std::optional<CMemPool>       pool_images;
    std::optional<CMemPool>       pool_code;
    std::optional<CMemPool>       pool_data;
    std::optional<nvg::DkRenderer> renderer;
    std::array<dk::Image, kFramebufferCount>        framebuffers;
    std::array<CMemPool::Handle, kFramebufferCount> fb_memory;

    // Depth/stencil — required for nanovg-deko3d's NVG_STENCIL_STROKES path.
    dk::Image                     depth;
    CMemPool::Handle              depth_memory;
};

inline Gfx& gfx_of(void* p) {
    return *static_cast<Gfx*>(p);
}

void deko3d_error_cb(void*, const char* context, DkResult result, const char* message) {
    log::write("[deko3d] ctx=%s rc=%d msg=%s\n", context, (int)result, message ? message : "");
}

} // namespace

App::App() {
    log::init_file();
    log::write("foyer %s starting\n", FOYER_DISPLAY_VERSION);

    init_fs();
    init_gfx();
    init_input();
}

App::~App() {
    exit_gfx();

    if (m_romfs_mounted) {
        romfsExit();
        m_romfs_mounted = false;
    }
    log::write("foyer shutting down\n");
    log::exit_file();
}

void App::init_fs() {
    // Mount the embedded romfs once for the lifetime of the process. libnx
    // 4.12 leaks across romfsInit/Exit cycles, so re-mounts later in startup
    // (e.g. when a renderer wants to load shaders) fail with OutOfMemory.
    if (R_SUCCEEDED(romfsInit())) {
        m_romfs_mounted = true;
    } else {
        log::write("[fs] romfsInit failed\n");
    }

    // Create the foyer data tree at SD root on first launch. fsFsCreateDirectory
    // returns the "already exists" result on subsequent runs; we ignore it.
    auto* fs = fsdevGetDeviceFileSystem("sdmc:");
    if (!fs) return;

    static const char* const kDirs[] = {
        "/foyer",
        "/foyer/cores",
        "/foyer/config",
        "/foyer/config/cores",
        "/foyer/data",
        "/foyer/data/cache",
        "/foyer/assets",
        "/foyer/assets/covers",
        "/foyer/assets/backgrounds",
        "/foyer/assets/systems",
        "/foyer/system",
        "/foyer/saves",
        "/foyer/states",
        // Default rom root. Lives under /foyer/ so the whole foyer state
        // tree is self-contained on the SD root.
        "/foyer/roms",
    };
    for (auto* p : kDirs) {
        fsFsCreateDirectory(fs, p);
    }
}

void App::init_gfx() {
    auto* gfx = new Gfx{};
    m_renderer_storage = gfx;

    gfx->device = dk::DeviceMaker{}
        .setCbDebug(deko3d_error_cb)
        .create();

    gfx->queue = dk::QueueMaker{gfx->device}
        .setFlags(DkQueueFlags_Graphics)
        .create();

    gfx->pool_images.emplace(gfx->device,
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
        16 * 1024 * 1024);
    gfx->pool_code.emplace(gfx->device,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code,
        128 * 1024);
    gfx->pool_data.emplace(gfx->device,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached,
        1 * 1024 * 1024);

    gfx->cmdbuf = dk::CmdBufMaker{gfx->device}.create();
    {
        const auto cmd_mem = gfx->pool_data->allocate(kStaticCmdSize);
        gfx->cmdbuf.addMemory(cmd_mem.getMemBlock(), cmd_mem.getOffset(), cmd_mem.getSize());
    }

    // Probe the natively-attached framebuffer size (handheld vs docked).
    const auto fb = nwindowGetDefault();
    NWindow* win = nwindowGetDefault();
    u32 w = 0, h = 0;
    nwindowGetDimensions(win, &w, &h);
    if (w == 0 || h == 0) {
        w = kLogicalW;
        h = kLogicalH;
    }
    m_fb_w = (int)w;
    m_fb_h = (int)h;

    // Stencil buffer (S8) — nanovg-deko3d needs one when NVG_STENCIL_STROKES
    // is enabled. Without it, path filling renders garbage.
    {
        dk::ImageLayout depth_layout;
        dk::ImageLayoutMaker{gfx->device}
            .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_S8)
            .setDimensions(m_fb_w, m_fb_h)
            .initialize(depth_layout);
        gfx->depth_memory = gfx->pool_images->allocate(
            depth_layout.getSize(), depth_layout.getAlignment());
        gfx->depth.initialize(depth_layout,
            gfx->depth_memory.getMemBlock(),
            gfx->depth_memory.getOffset());
    }

    // Build framebuffer images.
    auto fb_layout = dk::ImageLayoutMaker{gfx->device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_fb_w, m_fb_h);
    dk::ImageLayout image_layout;
    fb_layout.initialize(image_layout);

    std::array<DkImage const*, kFramebufferCount> fb_array{};
    for (unsigned i = 0; i < kFramebufferCount; i++) {
        gfx->fb_memory[i] = gfx->pool_images->allocate(
            image_layout.getSize(), image_layout.getAlignment());
        gfx->framebuffers[i].initialize(image_layout,
            gfx->fb_memory[i].getMemBlock(),
            gfx->fb_memory[i].getOffset());
        fb_array[i] = &gfx->framebuffers[i];
    }
    gfx->swapchain = dk::SwapchainMaker{gfx->device, win, fb_array}.create();

    // nanovg-deko3d renderer. The shader assets live under romfs:/shaders/.
    gfx->renderer.emplace(m_fb_w, m_fb_h,
        gfx->device, gfx->queue,
        *gfx->pool_images, *gfx->pool_code, *gfx->pool_data);

    m_vg = nvgCreateDk(&*gfx->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!m_vg) {
        log::write("[gfx] nvgCreateDk(AA) returned null, retrying without AA\n");
        m_vg = nvgCreateDk(&*gfx->renderer, 0);
    }
    if (!m_vg) {
        log::write("[gfx] nvgCreateDk failed; aborting\n");
        fatalThrow(MAKERESULT(354, 1));
    }

    log::write("[gfx] %dx%d, %u framebuffers\n", m_fb_w, m_fb_h, kFramebufferCount);

    // Load the system shared fonts so nanovg can render text.
    PlFontData std_font{}, ext_font{};
    if (R_SUCCEEDED(plGetSharedFontByType(&std_font, PlSharedFontType_Standard))) {
        const auto std_id = nvgCreateFontMem(m_vg, "default",
            (unsigned char*)std_font.address, std_font.size, 0);
        if (R_SUCCEEDED(plGetSharedFontByType(&ext_font, PlSharedFontType_NintendoExt))) {
            const auto ext_id = nvgCreateFontMem(m_vg, "ext",
                (unsigned char*)ext_font.address, ext_font.size, 0);
            nvgAddFallbackFontId(m_vg, std_id, ext_id);
        }
    } else {
        log::write("[gfx] plGetSharedFontByType(Standard) failed\n");
    }
}

void App::exit_gfx() {
    if (!m_renderer_storage) return;
    auto& gfx = gfx_of(m_renderer_storage);

    if (m_vg) {
        nvgDeleteDk(m_vg);
        m_vg = nullptr;
    }
    gfx.renderer.reset();
    if (gfx.queue) gfx.queue.waitIdle();
    gfx.depth_memory.destroy();
    for (auto& mem : gfx.fb_memory) mem.destroy();
    gfx.swapchain = nullptr;
    gfx.cmdbuf    = nullptr;
    gfx.pool_data.reset();
    gfx.pool_code.reset();
    gfx.pool_images.reset();
    gfx.queue     = nullptr;
    gfx.device    = nullptr;

    delete &gfx;
    m_renderer_storage = nullptr;
}

void App::init_input() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&m_pad);
}

bool App::tick() {
    if (m_quit) return false;
    if (!appletMainLoop()) return false;

    padUpdate(&m_pad);
    // No default quit gesture here — the caller decides when to call quit().
    // The browser binds + to exit; the player passes + through to the core
    // as the START button and uses a different combo (Phase 4 pause overlay).

    auto& gfx = gfx_of(m_renderer_storage);

    // Acquire a framebuffer from the swapchain.
    int slot = gfx.queue.acquireImage(gfx.swapchain);

    // Wait for the GPU to drain so we can safely re-record the cmdbuf.
    // (TODO Phase 4: switch to per-slot pre-recorded cmdlists for perf.)
    gfx.queue.waitIdle();

    // Begin recording. Set viewport, attach framebuffer + stencil, clear.
    gfx.cmdbuf.clear();
    dk::ImageView fb_view   { gfx.framebuffers[slot] };
    dk::ImageView depth_view{ gfx.depth };
    gfx.cmdbuf.bindRenderTargets(&fb_view, &depth_view);
    gfx.cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)m_fb_w, (float)m_fb_h, 0.0f, 1.0f } });
    gfx.cmdbuf.setScissors(0,  { { 0, 0, (uint32_t)m_fb_w, (uint32_t)m_fb_h } });
    gfx.cmdbuf.clearColor(0, DkColorMask_RGBA, 0.07f, 0.08f, 0.10f, 1.0f);
    gfx.cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

    // Bind default rasterizer / blend / colour-write states. Without these
    // the GPU has nothing to bind so nanovg's draws produce garbage.
    dk::RasterizerState  rs{};
    dk::ColorState       cs{};
    dk::ColorWriteState  cws{};
    gfx.cmdbuf.bindRasterizerState(rs);
    gfx.cmdbuf.bindColorState(cs);
    gfx.cmdbuf.bindColorWriteState(cws);
    gfx.queue.submitCommands(gfx.cmdbuf.finishList());

    // nanovg frame.
    nvgBeginFrame(m_vg, (float)m_fb_w, (float)m_fb_h, 1.0f);

    if (m_draw_fn) {
        m_draw_fn(m_vg, (float)m_fb_w, (float)m_fb_h);
    } else {
        nvgBeginPath(m_vg);
        nvgRect(m_vg, 0, 0, (float)m_fb_w, (float)m_fb_h);
        nvgFillColor(m_vg, nvgRGBA(0x12, 0x14, 0x18, 0xFF));
        nvgFill(m_vg);

        nvgFontSize(m_vg, 36.0f);
        nvgFillColor(m_vg, nvgRGBA(0xEE, 0xEE, 0xEE, 0xFF));
        nvgTextAlign(m_vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(m_vg, m_fb_w / 2.0f, m_fb_h / 2.0f - 24,
                "foyer " FOYER_DISPLAY_VERSION, nullptr);
        nvgFontSize(m_vg, 22.0f);
        nvgFillColor(m_vg, nvgRGBA(0x99, 0x9F, 0xA8, 0xFF));
        nvgText(m_vg, m_fb_w / 2.0f, m_fb_h / 2.0f + 24,
                "press + to exit", nullptr);
    }
    nvgEndFrame(m_vg);

    // nanovg-deko3d submits its own cmdlist via the queue inside nvgEndFrame.
    // Just present after.
    gfx.queue.presentImage(gfx.swapchain, slot);

    return !m_quit;
}

} // namespace foyer::platform

// libnx service init/teardown — referenced by both browser and player nros.
extern "C" {

void userAppInit(void) {
    Result rc;
    if (R_FAILED(rc = appletLockExit()))                 diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User))) diagAbortWithResult(rc);
    if (R_FAILED(rc = nifmInitialize(NifmServiceType_User))) diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize()))                  diagAbortWithResult(rc);

    // BSD sockets — needed by libcurl for the scrapers + RetroAchievements.
    // Use the applet-friendly profile so foyer plays nicely under hbloader.
    static const SocketInitConfig kSockets = {
        .tcp_tx_buf_size      = 1024 * 32,
        .tcp_rx_buf_size      = 1024 * 64,
        .tcp_tx_buf_max_size  = 1024 * 256,
        .tcp_rx_buf_max_size  = 1024 * 256,
        .udp_tx_buf_size      = 0x2400,
        .udp_rx_buf_size      = 0xA500,
        .sb_efficiency        = 4,
        .num_bsd_sessions     = 3,
        .bsd_service_type     = BsdServiceType_Auto,
    };
    if (R_FAILED(rc = socketInitialize(&kSockets)))      diagAbortWithResult(rc);

    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    socketExit();
    setExit();
    nifmExit();
    plExit();
    if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }
    appletUnlockExit();
}

} // extern "C"
