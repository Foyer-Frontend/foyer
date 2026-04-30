// Switch (libnx) stub of Common::PageFaultHandler.
//
// The upstream PSX recompiler installs a host page-fault handler so the
// fastmem path can recover from speculative loads/stores that miss. We don't
// build the recompiler on Switch (interpreter only), so nobody calls into
// these from the hot path. Provide trivial no-op stubs that satisfy the
// linker.

#include "common/page_fault_handler.h"

namespace Common::PageFaultHandler {

u32 GetHandlerCodeSize()
{
  return 0;
}

bool InstallHandler(const void* /*owner*/, void* /*start_pc*/, u32 /*code_size*/, Callback /*callback*/)
{
  return false;
}

bool RemoveHandler(const void* /*owner*/)
{
  return false;
}

} // namespace Common::PageFaultHandler
