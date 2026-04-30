// Switch stub for CDImage::OpenCHDImage.
//
// libchdr pulls in zstd and lzma which we don't carry on Switch — so we
// disable CHD support and return null when somebody hands us a .chd file.
// .bin/.cue, .img, .pbp, .ecm, .iso, .m3u, .mds all still work.

#include "common/cd_image.h"

std::unique_ptr<CDImage> CDImage::OpenCHDImage(const char* /*filename*/, OpenFlags /*open_flags*/,
                                               Common::Error* /*error*/)
{
  return nullptr;
}
