#include "output/ScreenSetup.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

using nlohmann::json;

namespace vj::output {

constexpr double kMaxDelayMs = 100;
namespace {

constexpr int kTessellation = 32;  // suficiente para que la perspectiva no muestre facetas
constexpr size_t kMaxScreens = 32, kMaxSlices = 256, kMaxPoints = 4096;
constexpr double kPi = 3.14159265358979323846;

json pointsJson(const std::vector<Point>& v) {
  json a = json::array();
  for (const auto& p : v) a.push_back({p.x, p.y});
  return a;
}

std::vector<Point> pointsFrom(const json& a) {
  std::vector<Point> out;
  if (!a.is_array() || a.size() > kMaxPoints) return out;
  for (const auto& p : a)
    if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) out.push_back({p[0].get<double>(), p[1].get<double>()});
  return out;
}

json masksJson(const std::vector<ScreenMask>& masks) {
  json a = json::array();
  for (const auto& m : masks) a.push_back({{"points", pointsJson(m.points)}, {"invert", m.invert}, {"smooth", m.smooth}});
  return a;
}

std::vector<ScreenMask> masksFrom(const json& a, bool invertByDefault) {
  std::vector<ScreenMask> out;
  for (const auto& jm : a) {
    ScreenMask m;
    m.points = pointsFrom(jm.value("points", json::array()));
    m.invert = jm.value("invert", invertByDefault);
    m.smooth = jm.value("smooth", false);
    if (m.points.size() >= 3) out.push_back(std::move(m));
  }
  return out;
}

double cross(Point a, Point b, Point c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool inTriangle(Point p, Point a, Point b, Point c) {
  const double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
  return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
}

bool segmentsCross(Point a, Point b, Point c, Point d) {
  const double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

Point catmull(Point a, Point b, Point c, Point d, double t) {
  const double t2 = t * t, t3 = t2 * t;
  const auto f = [&](double p0, double p1, double p2, double p3) {
    return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
  };
  return {f(a.x, b.x, c.x, d.x), f(a.y, b.y, c.y, d.y)};
}

// Coordenada de textura para un punto del slice. Los destinos tienen la fila 0 abajo.
void inputCoord(const Slice& s, Point unit, float& u, float& v) {
  double ux = s.flipH ? 1 - unit.x : unit.x, uy = s.flipV ? 1 - unit.y : unit.y;
  const double a = (s.inRotation + s.contentRotation) * kPi / 180, c = std::cos(a), sn = std::sin(a);
  const double dx = (ux - 0.5) * s.inW / s.contentScale, dy = (uy - 0.5) * s.inH / s.contentScale;
  const double x = s.inX + s.inW * (0.5 - s.contentX) + dx * c - dy * sn, y = s.inY + s.inH * (0.5 - s.contentY) + dx * sn + dy * c;
  u = static_cast<float>(x);
  v = static_cast<float>(1 - y);
}

}  // namespace

ScreenSetup defaultSetup() {
  ScreenSetup s;
  Screen screen;
  screen.id = 1;
  screen.name = "Pantalla 1";
  Slice slice;
  slice.id = 1;
  slice.name = "Slice 1";
  screen.slices.push_back(slice);
  s.screens.push_back(std::move(screen));
  return s;
}

json toJson(const ScreenSetup& s) {
  json screens = json::array();
  for (const auto& sc : s.screens) {
    json slices = json::array();
    for (const auto& l : sc.slices) {
      json corners = json::array();
      for (const auto& p : l.corners) corners.push_back({p.x, p.y});
      slices.push_back({{"id", l.id}, {"name", l.name}, {"sourceType", l.sourceType}, {"sourceIndex", l.sourceIndex}, {"ignoreBypass", l.ignoreBypass}, {"bypass", l.bypass},
                        {"inX", l.inX}, {"inY", l.inY}, {"inW", l.inW}, {"inH", l.inH}, {"inRotation", l.inRotation}, {"polygon", pointsJson(l.polygon)}, {"corners", corners},
                        {"warpMode", l.warpMode}, {"warpColumns", l.warpColumns}, {"warpRows", l.warpRows}, {"warp", pointsJson(l.warp)}, {"flipH", l.flipH}, {"flipV", l.flipV},
                        {"brightness", l.brightness}, {"contrast", l.contrast}, {"red", l.red}, {"green", l.green}, {"blue", l.blue}, {"isKey", l.isKey}, {"blackBg", l.blackBg},
                        {"blend", l.blend}, {"blendLeft", l.blendLeft}, {"blendRight", l.blendRight}, {"blendTop", l.blendTop}, {"blendBottom", l.blendBottom},
                        {"blendPower", l.blendPower}, {"blendLuminance", l.blendLuminance}, {"blendGamma", l.blendGamma}, {"blackLevel", l.blackLevel},
                        {"blendGammaRed", l.blendGammaRed}, {"blendGammaGreen", l.blendGammaGreen}, {"blendGammaBlue", l.blendGammaBlue},
                        {"contentX", l.contentX}, {"contentY", l.contentY}, {"contentScale", l.contentScale}, {"contentRotation", l.contentRotation},
                        {"outline", l.outline}, {"outlineOnly", l.outlineOnly}, {"outlineWidth", l.outlineWidth},
                        {"outlineRed", l.outlineRed}, {"outlineGreen", l.outlineGreen}, {"outlineBlue", l.outlineBlue}, {"masks", masksJson(l.masks)}});
    }
    screens.push_back({{"id", sc.id}, {"name", sc.name}, {"device", sc.device}, {"width", sc.width}, {"height", sc.height}, {"enabled", sc.enabled},
                       {"opacity", sc.opacity}, {"brightness", sc.brightness}, {"contrast", sc.contrast}, {"red", sc.red}, {"green", sc.green}, {"blue", sc.blue},
                       {"slices", std::move(slices)}, {"masks", masksJson(sc.masks)},
                       {"inputGuide", sc.inputGuide}, {"outputGuide", sc.outputGuide}, {"guideOpacity", sc.guideOpacity}, {"delay", sc.delay}});
  }
  return {{"format", "vjstudio-screens"}, {"version", 1}, {"name", s.name}, {"screens", std::move(screens)}};
}

ScreenSetup fromJson(const json& j) {
  if (!j.is_object() || j.value("format", "") != "vjstudio-screens") throw std::runtime_error("no es un escenario de pantallas");
  if (j.value("version", 0) > 1) throw std::runtime_error("el escenario es de una versión más nueva");
  ScreenSetup out;
  out.name = j.value("name", "Sin nombre");
  const json list = j.value("screens", json::array());
  if (list.empty() || list.size() > kMaxScreens) throw std::runtime_error("número de pantallas no válido");
  std::set<std::string> usedDevices;
  std::set<int> ids;
  for (const auto& js : list) {
    Screen sc;
    sc.id = js.value("id", 0);
    if (sc.id <= 0 || !ids.insert(sc.id).second) throw std::runtime_error("identificador de pantalla no válido");
    sc.name = js.value("name", "Pantalla " + std::to_string(sc.id));
    sc.device = js.value("device", "virtual");
    // Cada salida solo admite una pantalla: la repetida pasa a virtual.
    if (sc.device != "virtual" && !usedDevices.insert(sc.device).second) sc.device = "virtual";
    sc.width = std::clamp(js.value("width", 1920), 16, 16384);
    sc.height = std::clamp(js.value("height", 1080), 16, 16384);
    sc.enabled = js.value("enabled", true);
    sc.opacity = js.value("opacity", 1.0);
    sc.inputGuide = js.value("inputGuide", std::string{});
    sc.outputGuide = js.value("outputGuide", std::string{});
    sc.guideOpacity = js.value("guideOpacity", 0.5);
    sc.delay = std::clamp(js.value("delay", 0.0), 0.0, kMaxDelayMs);
    sc.brightness = js.value("brightness", 1.0);
    sc.contrast = js.value("contrast", 1.0);
    sc.red = js.value("red", 1.0);
    sc.green = js.value("green", 1.0);
    sc.blue = js.value("blue", 1.0);
    const json slices = js.value("slices", json::array());
    if (slices.size() > kMaxSlices) throw std::runtime_error("demasiados slices");
    int nextSlice = 1;
    for (const auto& jl : slices) {
      Slice l;
      l.id = jl.value("id", nextSlice);
      nextSlice = std::max(nextSlice, l.id) + 1;
      l.name = jl.value("name", "Slice " + std::to_string(l.id));
      l.sourceType = jl.value("sourceType", "composition");
      static const std::set<std::string> kinds = {"composition", "layer", "group", "preview", "screen", "bus"};
      if (!kinds.count(l.sourceType)) throw std::runtime_error("fuente de slice no válida");
      l.sourceIndex = jl.value("sourceIndex", 0);
      l.ignoreBypass = jl.value("ignoreBypass", false);
      l.bypass = jl.value("bypass", false);
      l.inX = jl.value("inX", 0.0);
      l.inY = jl.value("inY", 0.0);
      l.inW = jl.value("inW", 1.0);
      l.inH = jl.value("inH", 1.0);
      l.inRotation = jl.value("inRotation", 0.0);
      l.polygon = pointsFrom(jl.value("polygon", json::array()));
      l.masks = masksFrom(jl.value("masks", json::array()), false);
      if (l.polygon.size() >= 3 && selfIntersects(l.polygon)) throw std::runtime_error("un polígono se cruza consigo mismo");
      const auto c = pointsFrom(jl.value("corners", json::array()));
      if (c.size() == 4) std::copy(c.begin(), c.end(), l.corners.begin());
      l.warpMode = jl.value("warpMode", "linear");
      l.warpColumns = std::clamp(jl.value("warpColumns", 2), 2, 33);
      l.warpRows = std::clamp(jl.value("warpRows", 2), 2, 33);
      l.warp = pointsFrom(jl.value("warp", json::array()));
      if (!l.warp.empty() && l.warp.size() != static_cast<size_t>(l.warpColumns * l.warpRows)) l.warp.clear();
      l.flipH = jl.value("flipH", false);
      l.flipV = jl.value("flipV", false);
      l.brightness = jl.value("brightness", 1.0);
      l.contrast = jl.value("contrast", 1.0);
      l.red = jl.value("red", 1.0);
      l.green = jl.value("green", 1.0);
      l.blue = jl.value("blue", 1.0);
      l.isKey = jl.value("isKey", false);
      l.blackBg = jl.value("blackBg", false);
      l.blend = jl.value("blend", false);
      l.blendLeft = jl.value("blendLeft", 0.0);
      l.blendRight = jl.value("blendRight", 0.0);
      l.blendTop = jl.value("blendTop", 0.0);
      l.blendBottom = jl.value("blendBottom", 0.0);
      l.blendPower = jl.value("blendPower", 2.0);
      l.blendLuminance = jl.value("blendLuminance", 0.5);
      l.blendGamma = jl.value("blendGamma", 1.8);
      l.blackLevel = jl.value("blackLevel", 0.0);
      l.blendGammaRed = std::clamp(jl.value("blendGammaRed", 1.0), 0.2, 3.0);
      l.blendGammaGreen = std::clamp(jl.value("blendGammaGreen", 1.0), 0.2, 3.0);
      l.blendGammaBlue = std::clamp(jl.value("blendGammaBlue", 1.0), 0.2, 3.0);
      l.contentX = jl.value("contentX", 0.0);
      l.contentY = jl.value("contentY", 0.0);
      l.contentScale = std::clamp(jl.value("contentScale", 1.0), 0.05, 20.0);
      l.contentRotation = jl.value("contentRotation", 0.0);
      l.outline = jl.value("outline", false);
      l.outlineOnly = jl.value("outlineOnly", false);
      l.outlineWidth = std::clamp(jl.value("outlineWidth", 0.005), 0.0005, 0.1);
      l.outlineRed = jl.value("outlineRed", 1.0);
      l.outlineGreen = jl.value("outlineGreen", 1.0);
      l.outlineBlue = jl.value("outlineBlue", 1.0);
      sc.slices.push_back(std::move(l));
    }
    if (sc.slices.empty()) {
      Slice l;
      l.id = 1;
      l.name = "Slice 1";
      sc.slices.push_back(l);
    }
    sc.masks = masksFrom(js.value("masks", json::array()), true);
    out.screens.push_back(std::move(sc));
  }
  return out;
}

std::array<double, 9> perspective(const std::array<Point, 4>& q) {
  // Cuadrado unidad a cuadrilátero, según Heckbert.
  const double dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x, sx = q[0].x - q[1].x + q[2].x - q[3].x;
  const double dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y, sy = q[0].y - q[1].y + q[2].y - q[3].y;
  const double den = dx1 * dy2 - dx2 * dy1;
  const double g = den == 0 ? 0 : (sx * dy2 - dx2 * sy) / den;
  const double h = den == 0 ? 0 : (dx1 * sy - sx * dy1) / den;
  return {q[1].x - q[0].x + g * q[1].x, q[3].x - q[0].x + h * q[3].x, q[0].x,
          q[1].y - q[0].y + g * q[1].y, q[3].y - q[0].y + h * q[3].y, q[0].y,
          g, h, 1};
}

Point applyPerspective(const std::array<double, 9>& m, Point p) {
  const double w = m[6] * p.x + m[7] * p.y + m[8];
  return {(m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w};
}

Point evalWarp(const Slice& s, Point p) {
  if (s.warp.empty()) return p;
  const int cols = s.warpColumns, rows = s.warpRows;
  const auto cell = [&](int c, int r) { return s.warp[static_cast<size_t>(r) * cols + c]; };
  // Fuera de la rejilla se prolonga en línea recta: repetir el borde curvaría una rejilla plana.
  const auto at = [&](int c, int r) {
    const int cc = std::clamp(c, 0, cols - 1), rr = std::clamp(r, 0, rows - 1);
    Point p0 = cell(cc, rr);
    if (c != cc) {
      const Point in = cell(cc == 0 ? 1 : cols - 2, rr);
      p0 = {2 * p0.x - in.x, 2 * p0.y - in.y};
    }
    if (r != rr) {
      const Point in = cell(cc, rr == 0 ? 1 : rows - 2);
      const Point base = cell(cc, rr);
      p0 = {p0.x + base.x - in.x, p0.y + base.y - in.y};
    }
    return p0;
  };
  const double fx = std::clamp(p.x, 0.0, 1.0) * (cols - 1), fy = std::clamp(p.y, 0.0, 1.0) * (rows - 1);
  const int cx = std::min(static_cast<int>(fx), cols - 2), cy = std::min(static_cast<int>(fy), rows - 2);
  const double tx = fx - cx, ty = fy - cy;
  if (s.warpMode == "bezier") {
    std::array<Point, 4> row;
    for (int r = 0; r < 4; ++r) row[r] = catmull(at(cx - 1, cy - 1 + r), at(cx, cy - 1 + r), at(cx + 1, cy - 1 + r), at(cx + 2, cy - 1 + r), tx);
    return catmull(row[0], row[1], row[2], row[3], ty);
  }
  const Point a = at(cx, cy), b = at(cx + 1, cy), c = at(cx, cy + 1), d = at(cx + 1, cy + 1);
  return {(a.x * (1 - tx) + b.x * tx) * (1 - ty) + (c.x * (1 - tx) + d.x * tx) * ty,
          (a.y * (1 - tx) + b.y * tx) * (1 - ty) + (c.y * (1 - tx) + d.y * tx) * ty};
}

bool selfIntersects(const std::vector<Point>& p) {
  const size_t n = p.size();
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 2; j < n; ++j) {
      if (i == 0 && j == n - 1) continue;
      if (segmentsCross(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return true;
    }
  return false;
}

std::vector<Point> maskOutline(const ScreenMask& m) {
  if (!m.smooth) return m.points;
  constexpr int kSteps = 12;
  const size_t n = m.points.size();
  std::vector<Point> out;
  out.reserve(n * kSteps);
  for (size_t i = 0; i < n; ++i)
    for (int k = 0; k < kSteps; ++k)
      out.push_back(catmull(m.points[(i + n - 1) % n], m.points[i], m.points[(i + 1) % n], m.points[(i + 2) % n], double(k) / kSteps));
  return out;
}

std::vector<Point> triangulate(const std::vector<Point>& polygon) {
  std::vector<Point> out;
  if (polygon.size() < 3 || selfIntersects(polygon)) return out;
  double area = 0;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const Point a = polygon[i], b = polygon[(i + 1) % polygon.size()];
    area += a.x * b.y - b.x * a.y;
  }
  if (area == 0) return out;
  std::vector<size_t> idx(polygon.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  if (area < 0) std::reverse(idx.begin(), idx.end());
  // Recorte de orejas: suficiente para polígonos dibujados a mano.
  size_t guard = idx.size() * idx.size() + 8;
  while (idx.size() > 2 && guard-- > 0) {
    bool clipped = false;
    for (size_t i = 0; i < idx.size(); ++i) {
      const size_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
      const Point a = polygon[ia], b = polygon[ib], c = polygon[ic];
      if (cross(a, b, c) <= 0) continue;
      bool empty = true;
      for (size_t k : idx)
        if (k != ia && k != ib && k != ic && inTriangle(polygon[k], a, b, c)) empty = false;
      if (!empty) continue;
      out.insert(out.end(), {a, b, c});
      idx.erase(idx.begin() + static_cast<long>(i));
      clipped = true;
      break;
    }
    if (!clipped) return {};
  }
  return out;
}

double blendCurve(double x, double power, double luminance, double gamma) {
  x = std::clamp(x, 0.0, 1.0);
  const double y = x < 0.5 ? luminance * std::pow(2 * x, power) : 1 - (1 - luminance) * std::pow(2 * (1 - x), power);
  return std::pow(y, 1 / gamma);
}

Mesh buildOutline(const Slice& s, double aspect) {
  Mesh m;
  const auto h = perspective(s.corners);
  std::vector<Point> shape = s.polygon.size() >= 3 ? s.polygon : std::vector<Point>{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  // Cada lado se trocea para seguir la deformación.
  std::vector<Point> path;
  for (size_t i = 0; i < shape.size(); ++i) {
    const Point a = shape[i], b = shape[(i + 1) % shape.size()];
    for (int k = 0; k < kTessellation; ++k) {
      const double t = double(k) / kTessellation;
      path.push_back(applyPerspective(h, evalWarp(s, {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})));
    }
  }
  const auto ndc = [](double x, double y) {
    MeshVertex v{};
    v.x = static_cast<float>(x * 2 - 1);
    v.y = static_cast<float>(1 - y * 2);
    return v;
  };
  const double half = s.outlineWidth / 2;
  m.vertices.reserve(path.size() * 6);
  for (size_t i = 0; i < path.size(); ++i) {
    const Point a = path[i], b = path[(i + 1) % path.size()];
    const double dx = (b.x - a.x) * aspect, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len < 1e-9) continue;
    const double nx = -dy / len * half / aspect, ny = dx / len * half;
    const MeshVertex p[4] = {ndc(a.x + nx, a.y + ny), ndc(b.x + nx, b.y + ny), ndc(b.x - nx, b.y - ny), ndc(a.x - nx, a.y - ny)};
    for (int k : {0, 1, 2, 0, 2, 3}) m.vertices.push_back(p[k]);
  }
  return m;
}

Mesh buildMesh(const Slice& s) {
  Mesh m;
  const auto h = perspective(s.corners);
  const auto vertex = [&](Point unit) {
    const Point warped = evalWarp(s, unit);
    const Point screen = applyPerspective(h, warped);
    MeshVertex v{};
    v.x = static_cast<float>(screen.x * 2 - 1);
    v.y = static_cast<float>(1 - screen.y * 2);
    inputCoord(s, unit, v.u, v.v);
    v.sx = static_cast<float>(unit.x);
    v.sy = static_cast<float>(unit.y);
    return v;
  };
  if (s.polygon.size() >= 3) {
    for (const Point& p : triangulate(s.polygon)) m.vertices.push_back(vertex(p));
    return m;
  }
  m.vertices.reserve(kTessellation * kTessellation * 6);
  for (int y = 0; y < kTessellation; ++y)
    for (int x = 0; x < kTessellation; ++x) {
      const double x0 = double(x) / kTessellation, x1 = double(x + 1) / kTessellation;
      const double y0 = double(y) / kTessellation, y1 = double(y + 1) / kTessellation;
      for (Point p : {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y0}, Point{x1, y1}, Point{x0, y1}}) m.vertices.push_back(vertex(p));
    }
  return m;
}

}  // namespace vj::output
