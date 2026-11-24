#include "output/ScreenRenderer.h"

#include <algorithm>
#include <set>

namespace vj::output {
namespace {

constexpr int kDelayFrames = 16;  // 100 ms cabe de sobra incluso a 144 Hz
constexpr int kSliceMaskSize = 1024;

const char* kSliceFs = R"(#version 410 core
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform vec4 uParams[4];
in vec2 vUv;
in vec2 vSlice;
out vec4 color;
// uParams[0]: brillo, contraste, flags (1 clave, 2 fondo negro, 4 blending, 8 máscara), nivel de negro
// uParams[1]: rojo, verde, azul, gamma de fundido del rojo
// uParams[2]: bordes izquierdo, derecho, superior, inferior
// uParams[3]: potencia, luminancia, gamma, gamma de fundido del azul
// La gamma de fundido del verde va en la parte decimal de los flags, dividida entre 4.
float curve(float x) {
  x = clamp(x, 0.0, 1.0);
  float p = uParams[3].x, l = uParams[3].y;
  float y = x < 0.5 ? l * pow(2.0 * x, p) : 1.0 - (1.0 - l) * pow(2.0 * (1.0 - x), p);
  return pow(y, 1.0 / uParams[3].z);
}
void main() {
  vec4 c = texture(uTex0, vUv);
  int flags = int(uParams[0].z);
  float gammaGreen = fract(uParams[0].z) * 4.0;
  if ((flags & 1) != 0) c = vec4(vec3(c.a), 1.0);
  if ((flags & 2) != 0) c = vec4(c.rgb + (1.0 - c.a) * vec3(0.0), 1.0);
  vec3 k = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
  k = (k - 0.5) * uParams[0].y + 0.5;
  k = k * uParams[0].x * uParams[1].rgb;
  float inside = 1.0;
  vec3 fade = vec3(1.0);
  if ((flags & 4) != 0) {
    vec4 e = uParams[2];
    if (e.x > 0.0) inside *= curve(vSlice.x / e.x);
    if (e.y > 0.0) inside *= curve((1.0 - vSlice.x) / e.y);
    if (e.z > 0.0) inside *= curve(vSlice.y / e.z);
    if (e.w > 0.0) inside *= curve((1.0 - vSlice.y) / e.w);
    // El proyector no da negro puro: se levanta lo que no se solapa.
    k += uParams[0].w * step(0.999, inside);
    // Cada canal cae con su propia gamma para igualar proyectores con tinte distinto en la zona de solape.
    fade = pow(vec3(max(inside, 0.0001)), vec3(uParams[1].w, gammaGreen, uParams[3].w));
    fade *= step(0.0001, inside);
  }
  color = vec4(clamp(k, 0.0, 1.0) * c.a * fade, c.a * inside);
  if ((flags & 8) != 0) color *= texture(uTex1, vSlice).r;
}
)";

const char* kMaskFs = R"(#version 410 core
out vec4 color;
void main() { color = vec4(1.0); }
)";

const char* kColorFs = R"(#version 410 core
uniform vec4 uParams[4];
out vec4 color;
void main() { color = vec4(uParams[0].rgb, 1.0); }
)";

const char* kCopyFs = R"(#version 410 core
uniform sampler2D uTex0;
in vec2 vPos;
out vec4 color;
void main() { color = texture(uTex0, vPos); }
)";

const char* kFinalFs = R"(#version 410 core
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform vec4 uParams[4];
in vec2 vPos;
out vec4 color;
// uParams[0]: opacidad, brillo, contraste, -; uParams[1]: rgb
void main() {
  vec4 c = texture(uTex0, vPos);
  float m = texture(uTex1, vPos).r;
  vec3 k = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
  k = (k - 0.5) * uParams[0].z + 0.5;
  k = clamp(k * uParams[0].y * uParams[1].rgb, 0.0, 1.0);
  float a = c.a * uParams[0].x * m;
  color = vec4(k * a, a);
}
)";

// Rejilla y diagonal en coordenadas de la fuente: la diagonal cruza las pantallas sin saltos.
const char* kCardFs = R"(#version 410 core
in vec2 vPos;
out vec4 color;
void main() {
  vec2 g = fract(vPos * vec2(16.0, 9.0));
  vec2 w = fwidth(vPos * vec2(16.0, 9.0)) * 1.5;
  float line = max(step(g.x, w.x) + step(1.0 - w.x, g.x), step(g.y, w.y) + step(1.0 - w.y, g.y));
  float diag = step(abs(vPos.x - (1.0 - vPos.y)), 0.004);
  vec3 base = mix(vec3(0.05), vec3(0.75), clamp(line, 0.0, 1.0));
  color = vec4(mix(base, vec3(1.0, 0.2, 0.2), diag), 1.0);
}
)";

const char* kLabelFs = R"(#version 410 core
uniform sampler2D uTex0;
uniform vec4 uParams[4];
in vec2 vPos;
out vec4 color;
void main() {
  float a = texture(uTex0, vec2(vPos.x, 1.0 - vPos.y)).a;
  vec3 tone = 0.5 + 0.5 * cos(6.2832 * (uParams[0].x * 0.17 + vec3(0.0, 0.33, 0.67)));
  color = vec4(mix(tone * 0.35, vec3(1.0), a), 1.0);
}
)";

}  // namespace

ScreenRenderer::ScreenRenderer(gfx::IRenderBackend& gpu) : gpu_(gpu) {
  sliceProgram_ = gpu_.createMeshProgram(kSliceFs);
  maskProgram_ = gpu_.createMeshProgram(kMaskFs);
  colorProgram_ = gpu_.createMeshProgram(kColorFs);
  copyProgram_ = gpu_.createProgram(kCopyFs);
  finalProgram_ = gpu_.createProgram(kFinalFs);
  cardProgram_ = gpu_.createProgram(kCardFs);
  labelProgram_ = gpu_.createProgram(kLabelFs);
  const uint8_t none[4] = {0, 0, 0, 0};
  empty_ = gpu_.uploadTexture(gfx::kNoTexture, 1, 1, gfx::PixelFormat::RGBA8, 4, none);
  card_ = gpu_.createTarget(1920, 1080);
}

ScreenRenderer::~ScreenRenderer() {
  for (auto& [_, t] : targets_) {
    releaseRing(t);
    gpu_.destroyTarget(t.draft);
    gpu_.destroyTarget(t.mask);
    gpu_.destroyTarget(t.done);
  }
  for (auto& [_, m] : sliceMasks_) gpu_.destroyTarget(m.target);
  for (auto& [_, t] : labels_) gpu_.destroyTexture(t);
  gpu_.destroyTarget(card_);
  gpu_.destroyTexture(empty_);
}

void ScreenRenderer::releaseRing(Targets& t) {
  for (auto id : t.ring) gpu_.destroyTarget(id);
  t.ring.clear();
  t.stamps.clear();
  t.head = 0;
}

// Guarda el fotograma y devuelve el más reciente que ya tiene la antigüedad pedida.
gfx::TargetId ScreenRenderer::delayed(const Screen& s, Targets& t, double time) {
  if (s.delay <= 0) {
    releaseRing(t);
    return t.done;
  }
  if (t.ring.empty()) {
    t.ring.reserve(kDelayFrames);
    for (int i = 0; i < kDelayFrames; ++i) t.ring.push_back(gpu_.createTarget(s.width, s.height));
    t.stamps.assign(kDelayFrames, -1);
  }
  gpu_.beginPass(t.ring[t.head], true);
  gfx::DrawCall d;
  d.program = copyProgram_;
  d.textures[0] = gpu_.targetTexture(t.done);
  gpu_.draw(d);
  t.stamps[t.head] = time;
  gfx::TargetId best = t.ring[t.head];
  for (int back = 1; back < kDelayFrames; ++back) {
    const size_t i = (t.head + kDelayFrames - back) % kDelayFrames;
    if (t.stamps[i] < 0) break;
    best = t.ring[i];
    if (time - t.stamps[i] >= s.delay / 1000) break;
  }
  t.head = (t.head + 1) % kDelayFrames;
  return best;
}

void ScreenRenderer::ensure(const Screen& s) {
  Targets& t = targets_[s.id];
  if (t.w == s.width && t.h == s.height) return;
  releaseRing(t);
  if (t.draft) {
    gpu_.destroyTarget(t.draft);
    gpu_.destroyTarget(t.mask);
    gpu_.destroyTarget(t.done);
  }
  t.draft = gpu_.createTarget(s.width, s.height);
  t.mask = gpu_.createTarget(s.width, s.height);
  t.done = gpu_.createTarget(s.width, s.height);
  t.w = s.width;
  t.h = s.height;
}

void ScreenRenderer::drawMask(const ScreenMask& m, float top, float height, float color) {
  std::vector<MeshVertex> v;
  for (const Point& p : triangulate(maskOutline(m))) v.push_back({static_cast<float>(p.x * 2 - 1), top + height * static_cast<float>(p.y), 0, 0, 0, 0});
  if (v.empty()) return;
  gfx::MeshCall d;
  d.program = maskProgram_;
  d.texture = empty_;
  d.vertices = &v[0].x;
  d.vertexCount = static_cast<int>(v.size());
  d.colorMask = color;
  gpu_.drawMesh(d);
}

// Con máscaras normales solo se ve lo que cubren; las invertidas abren huecos.
gfx::TargetId ScreenRenderer::sliceMask(const Screen& s, const Slice& l) {
  const auto key = std::make_pair(s.id, l.id);
  auto found = sliceMasks_.find(key);
  if (l.masks.empty()) {
    if (found != sliceMasks_.end()) {
      gpu_.destroyTarget(found->second.target);
      sliceMasks_.erase(found);
    }
    return 0;
  }
  std::string signature;
  for (const auto& m : l.masks) {
    signature += m.invert ? 'i' : 'n';
    signature += m.smooth ? 's' : 'l';
    for (const Point& p : m.points) signature += std::to_string(p.x) + "," + std::to_string(p.y) + ";";
  }
  SliceMask& mask = sliceMasks_[key];
  if (!mask.target) mask.target = gpu_.createTarget(kSliceMaskSize, kSliceMaskSize);
  if (mask.signature == signature) return mask.target;
  mask.signature = std::move(signature);
  gpu_.beginPass(mask.target, true);
  const bool shape = std::any_of(l.masks.begin(), l.masks.end(), [](const ScreenMask& m) { return !m.invert; });
  if (!shape) gpu_.clearTarget(mask.target, 1, 1, 1, 1);
  for (const auto& m : l.masks) if (!m.invert) drawMask(m, -1, 2, 1);
  for (const auto& m : l.masks) if (m.invert) drawMask(m, -1, 2, 0);
  return mask.target;
}

void ScreenRenderer::renderScreen(const Screen& s, const SourceFn& source, bool testCard, bool identify, int number) {
  ensure(s);
  Targets& t = targets_[s.id];
  std::map<int, gfx::TargetId> masks;
  for (const Slice& l : s.slices) masks[l.id] = sliceMask(s, l);
  gpu_.beginPass(t.draft, true);
  for (const Slice& l : s.slices) {
    if (l.bypass) continue;
    gfx::TextureId tex = gfx::kNoTexture;
    if (testCard) tex = gpu_.targetTexture(card_);
    else if (l.sourceType == "screen") {
      auto it = out_.find(l.sourceIndex);
      tex = it != out_.end() && it->second.target ? gpu_.targetTexture(it->second.target) : empty_;
    } else {
      tex = source(l.sourceType, l.sourceIndex);
    }
    if (!tex) tex = empty_;
    const Mesh mesh = buildMesh(l);
    if (mesh.vertices.empty()) continue;
    const auto drawOutline = [&] {
      if (!l.outline && !l.outlineOnly) return;
      const Mesh line = buildOutline(l, double(s.width) / s.height);
      if (line.vertices.empty()) return;
      gfx::MeshCall c;
      c.program = colorProgram_;
      c.texture = empty_;
      c.vertices = &line.vertices[0].x;
      c.vertexCount = static_cast<int>(line.vertices.size());
      c.blendOver = true;
      c.params = {static_cast<float>(l.outlineRed), static_cast<float>(l.outlineGreen), static_cast<float>(l.outlineBlue), 1};
      gpu_.drawMesh(c);
    };
    if (l.outlineOnly) {
      drawOutline();
      continue;
    }
    gfx::MeshCall d;
    d.program = sliceProgram_;
    d.texture = tex;
    d.vertices = &mesh.vertices[0].x;
    d.vertexCount = static_cast<int>(mesh.vertices.size());
    d.blendOver = true;
    const int flags = (l.isKey ? 1 : 0) | (l.blackBg ? 2 : 0) | (l.blend ? 4 : 0) | (masks[l.id] ? 8 : 0);
    d.texture2 = masks[l.id] ? gpu_.targetTexture(masks[l.id]) : empty_;
    d.params = {static_cast<float>(l.brightness), static_cast<float>(l.contrast), static_cast<float>(flags) + static_cast<float>(l.blendGammaGreen / 4), static_cast<float>(l.blackLevel),
                static_cast<float>(l.red), static_cast<float>(l.green), static_cast<float>(l.blue), static_cast<float>(l.blendGammaRed),
                static_cast<float>(l.blendLeft), static_cast<float>(l.blendRight), static_cast<float>(l.blendTop), static_cast<float>(l.blendBottom),
                static_cast<float>(l.blendPower), static_cast<float>(l.blendLuminance), static_cast<float>(l.blendGamma), static_cast<float>(l.blendGammaBlue)};
    gpu_.drawMesh(d);
    drawOutline();
  }

  // Máscara de pantalla: rojo 1 donde se ve. Por defecto oculta lo de dentro del polígono.
  bool anyInverted = false;
  for (const auto& m : s.masks) anyInverted = anyInverted || m.invert;
  gpu_.beginPass(t.mask, true);
  if (s.masks.empty() || anyInverted) gpu_.clearTarget(t.mask, 1, 1, 1, 1);
  for (const auto& m : s.masks) drawMask(m, 1, -2, m.invert ? 0.f : 1.f);  // invertida: abre un agujero en el blanco

  gpu_.beginPass(t.done, true);
  gfx::DrawCall f;
  f.program = finalProgram_;
  f.textures[0] = gpu_.targetTexture(t.draft);
  f.textures[1] = gpu_.targetTexture(t.mask);
  f.params = {static_cast<float>(s.opacity), static_cast<float>(s.brightness), static_cast<float>(s.contrast), 0,
              static_cast<float>(s.red), static_cast<float>(s.green), static_cast<float>(s.blue), 0};
  gpu_.draw(f);

  if (identify) {
    auto& label = labels_[number];
    if (!label) {
      const auto frame = media::renderText(std::to_string(number), 360, 960, 540);
      label = gpu_.uploadTexture(gfx::kNoTexture, frame->width, frame->height, frame->format, frame->stride, frame->pixels.data());
    }
    gfx::DrawCall d;
    d.program = labelProgram_;
    d.textures[0] = label;
    d.params[0] = static_cast<float>(number);
    gpu_.draw(d);
  }
}

const std::map<int, RenderedScreen>& ScreenRenderer::render(const ScreenSetup& setup, const SourceFn& source, bool testCard, bool identify, double time) {
  if (testCard) {
    gpu_.beginPass(card_, true);
    gfx::DrawCall d;
    d.program = cardProgram_;
    gpu_.draw(d);
  }
  // Primero las pantallas de las que dependen otras; un ciclo se corta y queda en negro.
  std::map<int, const Screen*> byId;
  for (const auto& s : setup.screens) byId[s.id] = &s;
  std::set<int> done, active;
  out_.clear();
  std::function<void(const Screen&)> visit = [&](const Screen& s) {
    if (done.count(s.id) || active.count(s.id)) return;
    active.insert(s.id);
    for (const Slice& l : s.slices)
      if (l.sourceType == "screen" && byId.count(l.sourceIndex)) visit(*byId[l.sourceIndex]);
    active.erase(s.id);
    if (s.enabled) {
      int number = 1;
      for (const auto& other : setup.screens) {
        if (other.id == s.id) break;
        ++number;
      }
      renderScreen(s, source, testCard, identify, number);
      out_[s.id] = {delayed(s, targets_[s.id], time), s.width, s.height};
    }
    done.insert(s.id);
  };
  for (const auto& s : setup.screens) visit(s);
  return out_;
}

}  // namespace vj::output
