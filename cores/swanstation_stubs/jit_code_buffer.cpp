// Switch (libnx) stub of JitCodeBuffer.
//
// libnx exposes RW+X memory only via svcSetProcessMemoryPermission on a
// dynamically-allocated ELF segment, which is significantly more involved
// than the POSIX mmap path the upstream uses. The recompiler is disabled in
// our build (CPU runs through the interpreter), so we just need the symbol
// resolution to succeed — Allocate() returning false is fine because the
// recompiler is never asked to use it.

#include "common/jit_code_buffer.h"
#include <cstdlib>

JitCodeBuffer::JitCodeBuffer() = default;

JitCodeBuffer::JitCodeBuffer(u32 size, u32 far_code_size)
{
  Allocate(size, far_code_size);
}

JitCodeBuffer::JitCodeBuffer(void* buffer, u32 size, u32 far_code_size, u32 guard_size)
{
  Initialize(buffer, size, far_code_size, guard_size);
}

JitCodeBuffer::~JitCodeBuffer()
{
  Destroy();
}

bool JitCodeBuffer::Allocate(u32 /*size*/, u32 /*far_code_size*/)
{
  return false;
}

bool JitCodeBuffer::Initialize(void* /*buffer*/, u32 /*size*/, u32 /*far_code_size*/, u32 /*guard_size*/)
{
  return false;
}

void JitCodeBuffer::Destroy()
{
  if (m_owns_buffer && m_code_ptr)
    std::free(m_code_ptr);
  m_code_ptr = nullptr;
  m_free_code_ptr = nullptr;
  m_code_size = 0;
  m_total_size = 0;
  m_owns_buffer = false;
}

void JitCodeBuffer::Reset()
{
  m_free_code_ptr = m_code_ptr;
  m_code_used = 0;
  m_free_far_code_ptr = m_far_code_ptr;
  m_far_code_used = 0;
}

void JitCodeBuffer::ReserveCode(u32 size)
{
  m_code_reserve_size = size;
}

void JitCodeBuffer::CommitCode(u32 length)
{
  m_free_code_ptr += length;
  m_code_used += length;
}

void JitCodeBuffer::CommitFarCode(u32 length)
{
  m_free_far_code_ptr += length;
  m_far_code_used += length;
}

void JitCodeBuffer::Align(u32 /*alignment*/, u8 /*padding_value*/) {}

void JitCodeBuffer::FlushInstructionCache(void* /*address*/, u32 /*size*/) {}
