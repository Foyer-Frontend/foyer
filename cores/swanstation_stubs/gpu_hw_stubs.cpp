// Switch stub for hardware GPU backends (OpenGL/Vulkan/D3D11/D3D12).
//
// We don't ship glad, vulkan-loader, or D3D headers in the libnx player
// build, so the upstream gpu_hw_opengl.cpp / gpu_hw_vulkan.cpp / gpu_hw_d3d*
// translation units cannot compile here. The libretro frontend always
// requests the software renderer on Switch, but it still needs the symbols
// for the hardware backend factories and the LibretroOpenGL/Vulkan host
// display classes to resolve at link time. Provide them as defeated stubs.

#include "core/gpu.h"
#include "core/gpu_hw_opengl.h"
#include "core/gpu_hw_vulkan.h"
#include "core/host_display.h"

// ---------------------------------------------------------------------------
// GPU::CreateHardware*Renderer() factories — return nullptr so the caller
// falls back to GPU::CreateSoftwareRenderer().
// ---------------------------------------------------------------------------

std::unique_ptr<GPU> GPU::CreateHardwareOpenGLRenderer()
{
  return nullptr;
}

std::unique_ptr<GPU> GPU::CreateHardwareVulkanRenderer()
{
  return nullptr;
}

std::unique_ptr<GPU> GPU::CreateHardwareD3D11Renderer()
{
  return nullptr;
}

std::unique_ptr<GPU> GPU::CreateHardwareD3D12Renderer()
{
  return nullptr;
}

// ---------------------------------------------------------------------------
// LibretroOpenGLHostDisplay — every method returns the no-op equivalent.
// ---------------------------------------------------------------------------

LibretroOpenGLHostDisplay::LibretroOpenGLHostDisplay() = default;
LibretroOpenGLHostDisplay::~LibretroOpenGLHostDisplay() = default;

bool LibretroOpenGLHostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* /*cb*/, bool /*prefer_gles*/)
{
  return false;
}

HostDisplay::RenderAPI LibretroOpenGLHostDisplay::GetRenderAPI() const { return RenderAPI::None; }
void* LibretroOpenGLHostDisplay::GetRenderDevice() const { return nullptr; }
void* LibretroOpenGLHostDisplay::GetRenderContext() const { return nullptr; }

bool LibretroOpenGLHostDisplay::CreateRenderDevice(const WindowInfo&, std::string_view, bool, bool) { return false; }
bool LibretroOpenGLHostDisplay::InitializeRenderDevice(std::string_view, bool, bool) { return false; }
void LibretroOpenGLHostDisplay::DestroyRenderDevice() {}

void LibretroOpenGLHostDisplay::ResizeRenderWindow(s32, s32) {}
bool LibretroOpenGLHostDisplay::ChangeRenderWindow(const WindowInfo&) { return false; }

std::unique_ptr<HostDisplayTexture> LibretroOpenGLHostDisplay::CreateTexture(u32, u32, u32, u32, u32,
                                                                             HostDisplayPixelFormat, const void*, u32,
                                                                             bool)
{
  return nullptr;
}

bool LibretroOpenGLHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat) const { return false; }
bool LibretroOpenGLHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat, u32, u32, void**, u32*) { return false; }
void LibretroOpenGLHostDisplay::EndSetDisplayPixels() {}
bool LibretroOpenGLHostDisplay::SetDisplayPixels(HostDisplayPixelFormat, u32, u32, const void*, u32) { return false; }

bool LibretroOpenGLHostDisplay::Render() { return false; }

bool LibretroOpenGLHostDisplay::CreateResources() { return false; }
void LibretroOpenGLHostDisplay::DestroyResources() {}
void LibretroOpenGLHostDisplay::RenderSoftwareCursor() {}

// ---------------------------------------------------------------------------
// LibretroVulkanHostDisplay — same story.
// ---------------------------------------------------------------------------

LibretroVulkanHostDisplay::LibretroVulkanHostDisplay() = default;
LibretroVulkanHostDisplay::~LibretroVulkanHostDisplay() = default;

bool LibretroVulkanHostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* /*cb*/) { return false; }

HostDisplay::RenderAPI LibretroVulkanHostDisplay::GetRenderAPI() const { return RenderAPI::None; }
void* LibretroVulkanHostDisplay::GetRenderDevice() const { return nullptr; }
void* LibretroVulkanHostDisplay::GetRenderContext() const { return nullptr; }

bool LibretroVulkanHostDisplay::CreateRenderDevice(const WindowInfo&, std::string_view, bool, bool) { return false; }
bool LibretroVulkanHostDisplay::InitializeRenderDevice(std::string_view, bool, bool) { return false; }
void LibretroVulkanHostDisplay::DestroyRenderDevice() {}

void LibretroVulkanHostDisplay::ResizeRenderWindow(s32, s32) {}
bool LibretroVulkanHostDisplay::ChangeRenderWindow(const WindowInfo&) { return false; }

std::unique_ptr<HostDisplayTexture> LibretroVulkanHostDisplay::CreateTexture(u32, u32, u32, u32, u32,
                                                                             HostDisplayPixelFormat, const void*, u32,
                                                                             bool)
{
  return nullptr;
}

bool LibretroVulkanHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat) const { return false; }
bool LibretroVulkanHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat, u32, u32, void**, u32*) { return false; }
void LibretroVulkanHostDisplay::EndSetDisplayPixels() {}

bool LibretroVulkanHostDisplay::Render() { return false; }

bool LibretroVulkanHostDisplay::CreateResources() { return false; }
void LibretroVulkanHostDisplay::DestroyResources() {}
void LibretroVulkanHostDisplay::RenderSoftwareCursor() {}
