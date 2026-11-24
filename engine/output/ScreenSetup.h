#pragma once
#include <array>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vj::output {

struct Point {
  double x = 0, y = 0;
};

// Vértice listo para la GPU: posición en NDC, coordenada de textura y coordenada en el slice.
struct MeshVertex {
  float x, y, u, v, sx, sy;
};

struct Mesh {
  std::vector<MeshVertex> vertices;  // triángulos sueltos
};

// Todas las coordenadas de un escenario van normalizadas con el origen arriba a la izquierda:
// así un mapa funciona con cualquier resolución de composición o de pantalla.
struct ScreenMask {
  std::vector<Point> points;  // normalizadas en la pantalla o, en un slice, dentro de su entrada
  bool invert = true;         // como en el manual: en pantalla, por defecto oculta lo de dentro
  bool smooth = false;        // curva suave que pasa por los puntos
};

struct Slice {
  int id = 0;
  std::string name;
  std::string sourceType = "composition";  // composition, layer, group, preview, screen, bus (1 A, 2 B)
  int sourceIndex = 0;
  bool ignoreBypass = false;
  bool bypass = false;
  // Entrada: qué zona de la fuente se toma.
  double inX = 0, inY = 0, inW = 1, inH = 1, inRotation = 0;
  std::vector<Point> polygon;  // coordenadas 0..1 dentro de la entrada; vacío: rectángulo
  std::vector<ScreenMask> masks;  // recortes del slice; la deformación sigue a sus esquinas
  // Salida: dónde cae en la pantalla.
  std::array<Point, 4> corners = {Point{0, 0}, Point{1, 0}, Point{1, 1}, Point{0, 1}};
  std::string warpMode = "linear";  // linear o bezier
  int warpColumns = 2, warpRows = 2;
  std::vector<Point> warp;  // rejilla en coordenadas 0..1 del slice; vacío: sin deformar
  bool flipH = false, flipV = false;
  double brightness = 1, contrast = 1, red = 1, green = 1, blue = 1;
  bool isKey = false, blackBg = false;
  // Edge blending: ancho de cada borde, como fracción del slice.
  bool blend = false;
  double blendLeft = 0, blendRight = 0, blendTop = 0, blendBottom = 0;
  double blendPower = 2, blendLuminance = 0.5, blendGamma = 1.8, blackLevel = 0;
  double blendGammaRed = 1, blendGammaGreen = 1, blendGammaBlue = 1;  // ajuste fino por canal
  // Transformación del contenido dentro del slice: la forma de salida no cambia.
  double contentX = 0, contentY = 0, contentScale = 1, contentRotation = 0;
  // Contorno de la forma, en fracción del alto de pantalla.
  bool outline = false, outlineOnly = false;
  double outlineWidth = 0.005, outlineRed = 1, outlineGreen = 1, outlineBlue = 1;
};

struct Screen {
  int id = 0;
  std::string name;
  std::string device = "window";  // window, virtual, display:<índice>
  int width = 1920, height = 1080;
  bool enabled = true;
  double opacity = 1, brightness = 1, contrast = 1, red = 1, green = 1, blue = 1;
  std::vector<Slice> slices;
  std::vector<ScreenMask> masks;
  // Plantillas de ayuda: solo se ven en la ventana de pantallas, nunca en la salida.
  std::string inputGuide, outputGuide;
  double delay = 0;  // milisegundos, para alinear con pantallas más lentas
  double guideOpacity = 0.5;
};

struct ScreenSetup {
  std::string name = "Por defecto";
  std::vector<Screen> screens;
};

ScreenSetup defaultSetup();
nlohmann::json toJson(const ScreenSetup& s);
// Lanza std::runtime_error si el documento no es válido. Corrige lo que se pueda corregir:
// una salida ya usada por otra pantalla pasa a virtual, y una pantalla sin slices recibe uno.
ScreenSetup fromJson(const nlohmann::json& j);

// Homografía de las cuatro esquinas: (0,0) (1,0) (1,1) (0,1) a los puntos dados.
std::array<double, 9> perspective(const std::array<Point, 4>& corners);
Point applyPerspective(const std::array<double, 9>& h, Point p);

// Rejilla de deformación en un punto del slice. Sin rejilla devuelve el mismo punto.
Point evalWarp(const Slice& s, Point unit);

// Triángulos de un polígono simple. Vacío si el polígono se cruza consigo mismo o es degenerado.
// Contorno que se dibuja: curva cerrada si la máscara es suave, los puntos tal cual si no.
std::vector<Point> maskOutline(const ScreenMask& m);
std::vector<Point> triangulate(const std::vector<Point>& polygon);
bool selfIntersects(const std::vector<Point>& polygon);

// Curva de edge blending: x en 0..1 dentro del borde.
double blendCurve(double x, double power, double luminance, double gamma);

// Malla completa del slice. flipY: el origen del destino está abajo.
Mesh buildMesh(const Slice& s);
// Tiras del contorno de la forma, en coordenadas de vértice. El grosor se corrige por aspecto.
Mesh buildOutline(const Slice& s, double aspect);

}  // namespace vj::output
