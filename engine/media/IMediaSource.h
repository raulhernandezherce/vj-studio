#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gfx/IRenderBackend.h"

namespace vj::media {

struct VideoFrame {
  int width = 0;
  int height = 0;
  int stride = 0;
  gfx::PixelFormat format = gfx::PixelFormat::BGRA8;
  double time = 0;  // segundos dentro del archivo
  std::vector<uint8_t> pixels;
};
using FramePtr = std::shared_ptr<const VideoFrame>;

struct MediaInfo {
  double duration = 0;     // 0 = sin línea de tiempo propia (imágenes y generadores fijos)
  int width = 0;
  int height = 0;
  bool straightAlpha = false;  // el contenido trae alfa sin premultiplicar
};

// Toda fuente de imagen: archivo, generador y, más adelante, cámara, NDI o Spout.
// El acceso es aleatorio: el reloj del clip puede ir atrás, saltar o rebotar.
class IMediaSource {
 public:
  virtual ~IMediaSource() = default;
  virtual MediaInfo info() const = 0;
  // El fotograma más cercano ya disponible a t; nullptr si aún no hay ninguno.
  virtual FramePtr frameAt(double t) = 0;
  // Libera lo que se pueda: el clip sigue cargado pero ya no se reproduce.
  virtual void release() {}
};

using SourcePtr = std::shared_ptr<IMediaSource>;

// Rutas vj://pattern, vj://solid/RRGGBBAA y vj://circle son generadores internos.
// Elige el decodificador por ruta; un hito nuevo añade aquí su backend.
SourcePtr openMedia(const std::string& path, std::string& error);

// Lee un archivo .cube 3D y lo aplana en una tira de N cortes de N×N. Lanza si no es válido.
FramePtr loadCubeLut(const std::string& path);

// Cámaras y capturadoras visibles para el sistema. No pide permiso de cámara.
struct CaptureDevice {
  std::string id, name;
};
std::vector<CaptureDevice> listCaptureDevices();

// Escribe vídeo en un hilo propio: render nunca espera al disco. Si el disco no da abasto,
// se pierden fotogramas y se cuentan, en vez de frenar la salida.
class VideoRecorder {
 public:
  virtual ~VideoRecorder() = default;
  // rgba con la fila superior primero; time en segundos, creciente.
  virtual void addFrame(std::vector<uint8_t> rgba, double time) = 0;
  // Termina el archivo y espera a que quede cerrado. Devuelve false si falló.
  virtual bool finish() = 0;
  virtual long framesWritten() const = 0;
  virtual long framesDropped() const = 0;
};
// codec: "H.264", "HEVC" o "ProRes 422". Lanza std::runtime_error si no puede empezar.
std::unique_ptr<VideoRecorder> makeRecorder(const std::string& path, int width, int height, const std::string& codec);

SourcePtr makeGenerator(const std::string& path, int width = 1280, int height = 720);

// Codificación de imagen. Los píxeles van en RGBA con la fila superior primero.
bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba);
// Devuelve una URL data:image/jpeg; cadena vacía si no se puede.
// Texto blanco sobre transparente, centrado. Premultiplicado.
FramePtr renderText(const std::string& text, double size, int width, int height);
std::string thumbnailDataUrl(const VideoFrame& frame, int width = 160, int height = 90);

}  // namespace vj::media
