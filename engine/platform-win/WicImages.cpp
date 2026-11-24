// Imágenes con WIC y texto con GDI. Equivale a ImageSource e ImageCodec.
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

#include "media/IMediaSource.h"
#include "platform-win/WinCommon.h"

namespace vj::media {
namespace {

using win::Com;
using win::ComScope;

bool factory(Com<IWICImagingFactory>& f) {
  return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f.put())));
}

class Image final : public IMediaSource {
 public:
  explicit Image(FramePtr frame) : frame_(std::move(frame)) {}
  MediaInfo info() const override { return {0, frame_->width, frame_->height, false}; }
  FramePtr frameAt(double) override { return frame_; }

 private:
  FramePtr frame_;
};

// Codifica a un flujo en memoria con el contenedor pedido.
bool encode(REFGUID container, const WICPixelFormatGUID& format, int w, int h, int stride, const uint8_t* px, IStream* stream) {
  Com<IWICImagingFactory> wic;
  Com<IWICBitmapEncoder> encoder;
  Com<IWICBitmapFrameEncode> frame;
  if (!factory(wic) || FAILED(wic->CreateEncoder(container, nullptr, encoder.put())) || FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) return false;
  if (FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr))) return false;
  frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h));
  WICPixelFormatGUID wanted = format;
  frame->SetPixelFormat(&wanted);
  if (wanted != format) return false;
  if (FAILED(frame->WritePixels(static_cast<UINT>(h), static_cast<UINT>(stride), static_cast<UINT>(stride * h), const_cast<BYTE*>(px)))) return false;
  return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::string base64(const uint8_t* data, size_t size) {
  static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((size + 2) / 3 * 4);
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t v = (uint32_t{data[i]} << 16) | (i + 1 < size ? uint32_t{data[i + 1]} << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += i + 1 < size ? table[(v >> 6) & 63] : '=';
    out += i + 2 < size ? table[v & 63] : '=';
  }
  return out;
}

}  // namespace

SourcePtr openImage(const std::string& path, std::string& error) {
  ComScope com;
  Com<IWICImagingFactory> wic;
  Com<IWICBitmapDecoder> decoder;
  Com<IWICBitmapFrameDecode> source;
  Com<IWICFormatConverter> converter;
  if (!factory(wic) || FAILED(wic->CreateDecoderFromFilename(win::wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
      FAILED(decoder->GetFrame(0, source.put())) || FAILED(wic->CreateFormatConverter(converter.put())) ||
      FAILED(converter->Initialize(source.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
    error = "no se pudo leer la imagen";
    return nullptr;
  }
  UINT w = 0, h = 0;
  converter->GetSize(&w, &h);
  auto f = std::make_shared<VideoFrame>();
  f->width = static_cast<int>(w);
  f->height = static_cast<int>(h);
  f->stride = f->width * 4;
  f->format = gfx::PixelFormat::BGRA8;
  f->pixels.resize(static_cast<size_t>(f->stride) * h);
  if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(f->stride), static_cast<UINT>(f->pixels.size()), f->pixels.data()))) {
    error = "no se pudo leer la imagen";
    return nullptr;
  }
  return std::make_shared<Image>(std::move(f));
}

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
  ComScope com;
  Com<IStream> stream;
  if (FAILED(SHCreateStreamOnFileEx(win::wide(path).c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, stream.put()))) return false;
  return encode(GUID_ContainerFormatPng, GUID_WICPixelFormat32bppRGBA, width, height, width * 4, rgba.data(), stream.p);
}

std::string thumbnailDataUrl(const VideoFrame& f, int width, int height) {
  ComScope com;
  std::vector<uint8_t> px(static_cast<size_t>(width) * height * 3);
  const bool bgra = f.format == gfx::PixelFormat::BGRA8;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const uint8_t* s = f.pixels.data() + static_cast<size_t>(y * f.height / height) * f.stride + static_cast<size_t>(x * f.width / width) * 4;
      uint8_t* d = px.data() + (static_cast<size_t>(y) * width + x) * 3;
      d[0] = s[bgra ? 0 : 2];
      d[1] = s[1];
      d[2] = s[bgra ? 2 : 0];
    }
  Com<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) return "";
  if (!encode(GUID_ContainerFormatJpeg, GUID_WICPixelFormat24bppBGR, width, height, width * 3, px.data(), stream.p)) return "";
  HGLOBAL memory = nullptr;
  GetHGlobalFromStream(stream.p, &memory);
  const auto* data = static_cast<const uint8_t*>(GlobalLock(memory));
  if (!data) return "";
  STATSTG stat{};
  stream->Stat(&stat, STATFLAG_NONAME);
  std::string out = "data:image/jpeg;base64," + base64(data, static_cast<size_t>(stat.cbSize.QuadPart));
  GlobalUnlock(memory);
  return out;
}

FramePtr renderText(const std::string& text, double size, int width, int height) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof info.bmiHeader;
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;  // fila superior primero
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    DeleteDC(dc);
    return nullptr;
  }
  HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
  HFONT font = CreateFontW(-static_cast<int>(size), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT r{0, 0, width, height};
  const std::wstring w = win::wide(text);
  RECT measure = r;
  DrawTextW(dc, w.c_str(), -1, &measure, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
  const int top = std::max(0, (height - static_cast<int>(measure.bottom - measure.top)) / 2);
  r.top = top;
  DrawTextW(dc, w.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK);
  GdiFlush();
  auto f = std::make_shared<VideoFrame>();
  f->width = width;
  f->height = height;
  f->stride = width * 4;
  f->format = gfx::PixelFormat::BGRA8;
  f->pixels.resize(static_cast<size_t>(f->stride) * height);
  std::memcpy(f->pixels.data(), bits, f->pixels.size());
  // GDI no escribe alfa: blanco sobre negro, el brillo es la cobertura y ya está premultiplicado.
  for (size_t i = 0; i < f->pixels.size(); i += 4) f->pixels[i + 3] = f->pixels[i + 1];
  SelectObject(dc, oldFont);
  SelectObject(dc, oldBitmap);
  DeleteObject(font);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return f;
}

}  // namespace vj::media
