// Switch (libnx) stub of Common::MemoryArena.
//
// libnx has no shm_open/mmap-with-fixed-address, which the upstream
// implementation relies on for the PSX bus' "fastmem" view aliasing. We
// replace it with a plain std::malloc-backed arena: each CreateView returns a
// fresh malloc()'d block of the requested size. The PSX fastmem path is
// disabled at compile time (no WITH_MMAP_FASTMEM define), so the only views
// the rest of the code creates are the small per-region RAM/BIOS blocks.

#include "common/memory_arena.h"
#include <cstdlib>
#include <cstring>

namespace Common {

MemoryArena::MemoryArena() = default;

MemoryArena::~MemoryArena()
{
  Destroy();
}

void* MemoryArena::FindBaseAddressForMapping(size_t /*size*/)
{
  // No fixed-address mmap on Switch. The fastmem path that needs this is
  // compiled out — return nullptr so any caller fails gracefully.
  return nullptr;
}

bool MemoryArena::IsValid() const
{
  return m_size > 0;
}

bool MemoryArena::Create(size_t size, bool writable, bool executable)
{
  if (IsValid())
    Destroy();

  m_size = size;
  m_writable = writable;
  m_executable = executable;
  return true;
}

void MemoryArena::Destroy()
{
  m_size = 0;
}

std::optional<MemoryArena::View> MemoryArena::CreateView(size_t offset, size_t size, bool writable, bool executable,
                                                         void* fixed_address)
{
  void* base_pointer = CreateViewPtr(offset, size, writable, executable, fixed_address);
  if (!base_pointer)
    return std::nullopt;

  return View(this, base_pointer, offset, size, writable);
}

std::optional<MemoryArena::View> MemoryArena::CreateReservedView(size_t size, void* fixed_address)
{
  void* base_pointer = CreateReservedPtr(size, fixed_address);
  if (!base_pointer)
    return std::nullopt;

  return View(this, base_pointer, View::RESERVED_REGION_OFFSET, size, false);
}

void* MemoryArena::CreateViewPtr(size_t /*offset*/, size_t size, bool /*writable*/, bool /*executable*/,
                                 void* fixed_address)
{
  // Fixed-address mappings would require Switch's svcMapMemory + manual VA
  // bookkeeping; for the interpreter-only build we only see fixed_address ==
  // nullptr requests, so refuse anything else.
  if (fixed_address != nullptr)
    return nullptr;

  void* p = std::malloc(size);
  if (!p)
    return nullptr;
  std::memset(p, 0, size);
  m_num_views.fetch_add(1);
  return p;
}

bool MemoryArena::FlushViewPtr(void* /*address*/, size_t /*size*/)
{
  return true;
}

bool MemoryArena::ReleaseViewPtr(void* address, size_t /*size*/)
{
  if (!address)
    return false;
  std::free(address);
  m_num_views.fetch_sub(1);
  return true;
}

void* MemoryArena::CreateReservedPtr(size_t /*size*/, void* /*fixed_address*/)
{
  return nullptr;
}

bool MemoryArena::ReleaseReservedPtr(void* /*address*/, size_t /*size*/)
{
  return false;
}

bool MemoryArena::SetPageProtection(void* /*address*/, size_t /*length*/, bool /*readable*/, bool /*writable*/,
                                    bool /*executable*/)
{
  // No mprotect on libnx — pretend it worked. Fastmem path is disabled.
  return true;
}

MemoryArena::View::View(MemoryArena* parent, void* base_pointer, size_t arena_offset, size_t mapping_size,
                        bool writable)
  : m_parent(parent), m_base_pointer(base_pointer), m_arena_offset(arena_offset), m_mapping_size(mapping_size),
    m_writable(writable)
{
}

MemoryArena::View::View(View&& view)
  : m_parent(view.m_parent), m_base_pointer(view.m_base_pointer), m_arena_offset(view.m_arena_offset),
    m_mapping_size(view.m_mapping_size)
{
  view.m_parent = nullptr;
  view.m_base_pointer = nullptr;
  view.m_arena_offset = 0;
  view.m_mapping_size = 0;
}

MemoryArena::View::~View()
{
  if (m_parent && m_base_pointer)
  {
    if (m_arena_offset != RESERVED_REGION_OFFSET)
      m_parent->ReleaseViewPtr(m_base_pointer, m_mapping_size);
    else
      m_parent->ReleaseReservedPtr(m_base_pointer, m_mapping_size);
  }
}

} // namespace Common
