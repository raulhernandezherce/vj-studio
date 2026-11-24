#include <algorithm>
#include <cctype>

#include "media/IMediaSource.h"

namespace vj::media {

#if defined(__APPLE__) || defined(_WIN32)
SourcePtr openAvfMedia(const std::string& path, std::string& error);
SourcePtr openImage(const std::string& path, std::string& error);
SourcePtr openCamera(const std::string& id, std::string& error);
#endif

SourcePtr openMedia(const std::string& path, std::string& error) {
#if defined(__APPLE__) || defined(_WIN32)
  if (path.rfind("vj://camera/", 0) == 0) return openCamera(path.substr(12), error);
#endif
  if (path.rfind("vj://", 0) == 0) {
    auto g = makeGenerator(path);
    if (!g) error = "generador desconocido";
    return g;
  }
#if defined(__APPLE__) || defined(_WIN32)
  std::string ext = path.substr(path.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  for (const char* e : {"png", "jpg", "jpeg", "tif", "tiff", "bmp", "gif", "webp", "heic"})
    if (ext == e) return openImage(path, error);
  return openAvfMedia(path, error);
#else
  (void)path;
  error = "sin decodificador en esta plataforma";
  return nullptr;
#endif
}

}  // namespace vj::media
