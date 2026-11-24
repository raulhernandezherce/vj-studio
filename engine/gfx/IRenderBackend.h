#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vj::gfx {

using TextureId = uint32_t;
using TargetId = uint32_t;
using ProgramId = uint32_t;
inline constexpr TextureId kNoTexture = 0;
inline constexpr TargetId kNoTarget = 0;

enum class PixelFormat { RGBA8, BGRA8 };

using Mat3 = std::array<float, 9>;  // por columnas
inline constexpr Mat3 kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1};

struct DrawParams {
  float opacity = 1.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
};

// Una pasada de pantalla completa o de un cuadro transformado.
// Los programas reciben uTex0..2, uMatrix, uParams[4], uMode, vUv (de textura) y vPos (de destino).
struct DrawCall {
  ProgramId program = 0;
  std::array<TextureId, 3> textures = {kNoTexture, kNoTexture, kNoTexture};
  Mat3 matrix = kIdentity;
  std::array<float, 16> params = {};
  int mode = 0;
  bool flipV = false;  // true si uTex0 viene de un destino de render
};

// Malla arbitraria: 6 floats por vértice (posición NDC, textura, coordenada propia).
// Las mallas son lo que permite slices deformados y polígonos.
struct MeshCall {
  ProgramId program = 0;
  TextureId texture = kNoTexture;
  TextureId texture2 = kNoTexture;
  const float* vertices = nullptr;
  int vertexCount = 0;
  std::array<float, 16> params = {};
  bool blendOver = false;     // premultiplicado, encima de lo que haya
  float colorMask = -1;       // >= 0: pinta ese gris opaco (máscaras)
};

// Capa de abstracción gráfica: nada fuera de engine/gfx conoce OpenGL.
// Las mezclas se escriben en GLSL 4.10 como datos; otro backend debe traducirlas.
class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;
  virtual std::string name() const = 0;

  virtual TextureId uploadTexture(TextureId id, int width, int height, PixelFormat format, int stride,
                                  const void* pixels) = 0;
  virtual void destroyTexture(TextureId id) = 0;

  // deep: 16 bits de coma flotante por canal, para cadenas que no deben perder precisión.
  virtual TargetId createTarget(int width, int height, bool deep = false) = 0;
  virtual void destroyTarget(TargetId id) = 0;
  virtual TextureId targetTexture(TargetId id) = 0;

  // fragment es el cuerpo GLSL completo. Lanza std::runtime_error si no compila.
  virtual ProgramId createProgram(const std::string& fragment) = 0;
  // Programa para drawMesh. Recibe vUv y vSlice.
  virtual ProgramId createMeshProgram(const std::string& fragment) = 0;

  virtual void beginPass(TargetId target, bool clear) = 0;
  virtual void draw(const DrawCall& call) = 0;
  virtual void drawMesh(const MeshCall& call) = 0;
  virtual void clearTarget(TargetId target, float r, float g, float b, float a) = 0;
  // Varias ventanas comparten texturas pero no VAO: identifica la ventana activa en present().
  virtual void setPresentContext(int id) = 0;
  virtual void releasePresentContext(int id) = 0;

  // Cede la GPU a código ajeno que dibuja con OpenGL (plugins FFGL). Deja `output` activo y el
  // contexto en estado por defecto, y lo restablece al volver. Solo lo admite el backend OpenGL.
  virtual bool runExternal(TargetId output, TargetId input, const std::function<void(uint32_t inputTexture, uint32_t framebuffer)>& fn) = 0;

  // Copia una textura de destino a la ventana activa, sobre negro.
  virtual void present(TextureId texture, int width, int height, const DrawParams& params) = 0;

  // RGBA con la fila superior primero. Funciona con la ventana oculta.
  virtual std::vector<uint8_t> readback(TargetId target, int width, int height) = 0;
};

// Requiere un contexto OpenGL 4.1 core activo en el hilo que llama.
std::unique_ptr<IRenderBackend> makeOpenGL41Backend();
// Para pruebas y modo sin ventana.
std::unique_ptr<IRenderBackend> makeNullBackend();

}  // namespace vj::gfx
