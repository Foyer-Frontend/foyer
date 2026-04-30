// Switch stub replacement for src/core/gpu_hw_opengl.h.
//
// The upstream header pulls in glad and the full GL::* helper namespace; we
// don't have either on libnx, and the player only ever runs the software
// renderer. Provide stripped-down forward declarations of the two classes
// that libretro_host_interface.cpp references.

#pragma once
#include "core/host_display.h"
#include <libretro.h>
#include <memory>

class LibretroOpenGLHostDisplay final : public HostDisplay
{
public:
  LibretroOpenGLHostDisplay();
  ~LibretroOpenGLHostDisplay();

  static bool RequestHardwareRendererContext(retro_hw_render_callback* cb, bool prefer_gles);

  RenderAPI GetRenderAPI() const override;
  void* GetRenderDevice() const override;
  void* GetRenderContext() const override;

  bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                          bool threaded_presentation) override;
  bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                              bool threaded_presentation) override;
  void DestroyRenderDevice() override;

  void ResizeRenderWindow(s32 new_window_width, s32 new_window_height) override;

  bool ChangeRenderWindow(const WindowInfo& new_wi) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                    HostDisplayPixelFormat format, const void* data, u32 data_stride,
                                                    bool dynamic = false) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                             u32* out_pitch) override;
  void EndSetDisplayPixels() override;
  bool SetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, const void* buffer, u32 pitch) override;

  bool Render() override;

protected:
  bool CreateResources() override;
  void DestroyResources() override;
  void RenderSoftwareCursor() override;
};
