#include "compositor/Renderer.h"

#include <algorithm>

#include "core/generated/BlendModes.gen.h"
#include "core/generated/Effects.gen.h"

namespace vj::compositor {
namespace {

const char* kCommon = R"(#version 410 core
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform vec4 uParams[4];
uniform int uMode;
in vec2 vUv;
in vec2 vPos;
out vec4 color;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
)";

const char* kClipFs = R"(
void main() {
  vec4 c = texture(uTex0, vUv);
  // uParams[0]: máscara de canales y usar alfa. uParams[1].x: opacidad. .y: alfa recta.
  if (uParams[1].y > 0.5) c.rgb *= c.a;
  if (uParams[0].w < 0.5) { c.rgb = c.a > 0.0 ? c.rgb / c.a : c.rgb; c.a = 1.0; }
  c.rgb *= uParams[0].xyz;
  color = c * uParams[1].x;
}
)";

const char* kMaskFs = R"(
void main() {
  vec4 a = texture(uTex0, vPos);
  float l = dot(texture(uTex1, vPos).rgb, LUMA);
  color = a * mix(1.0, l, uParams[0].x);
}
)";

const char* kCopyFs = R"(
void main() { color = texture(uTex0, vPos); }
)";

const char* kMixFs = R"(
void main() { color = mix(texture(uTex0, vPos), texture(uTex1, vPos), uParams[0].x); }
)";

const char* kBlendHelpers = R"(
vec3 unpremul(vec4 c) { return c.a > 0.0 ? clamp(c.rgb / c.a, 0.0, 1.0) : vec3(0.0); }
vec4 over(vec4 b, vec4 s) { return s + b * (1.0 - s.a); }
vec3 burn(vec3 b, vec3 s) {
  vec3 r = 1.0 - min(vec3(1.0), (1.0 - b) / max(s, vec3(1e-6)));
  return mix(r, vec3(1.0), step(1.0, b));
}
vec3 dodge(vec3 b, vec3 s) {
  vec3 r = min(vec3(1.0), b / max(1.0 - s, vec3(1e-6)));
  return mix(r, vec3(0.0), step(b, vec3(0.0)));
}
vec3 hardlight(vec3 b, vec3 s) {
  return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, s));
}
vec3 softlight(vec3 b, vec3 s) {
  vec3 d = mix(sqrt(b), ((16.0 * b - 12.0) * b + 4.0) * b, step(b, vec3(0.25)));
  return mix(b - (1.0 - 2.0 * s) * b * (1.0 - b), b + (2.0 * s - 1.0) * (d - b), step(0.5, s));
}
vec4 mixed(vec4 b, vec4 ss, vec3 cb, vec3 bl) {
  // Porter-Duff con función de mezcla: fuera del solapamiento cada capa queda tal cual.
  return vec4((1.0 - b.a) * ss.rgb + (1.0 - ss.a) * b.rgb + ss.a * b.a * bl, ss.a + b.a - ss.a * b.a);
}
)";

}  // namespace

std::string Renderer::commonShader() { return kCommon; }
std::string Renderer::clipShader() { return std::string(kCommon) + kClipFs; }

std::string Renderer::blendShader() {
  std::string out = kCommon;
  out += kBlendHelpers;
  out += "void main() {\n  vec4 b = texture(uTex0, vPos), s = texture(uTex1, vPos), r = b;\n";
  out += "  float o = uParams[0].x;\n  vec3 cb = unpremul(b), cs = unpremul(s);\n";
  out += "  float l = dot(cs, LUMA);\n  vec4 ss = s * o;\n";
  const auto& modes = schema::blendModes();
  for (size_t i = 0; i < modes.size(); ++i) {
    out += "  " + std::string(i ? "else " : "") + "if (uMode == " + std::to_string(i) + ") { ";
    if (modes[i].raw) out += modes[i].raw;
    else out += "r = mixed(b, ss, cb, " + std::string(modes[i].blend) + ");";
    out += " } // " + std::string(modes[i].name) + "\n";
  }
  out += "  color = clamp(r, 0.0, 1.0);\n}\n";
  return out;
}

std::string Renderer::transitionShader() {
  std::string out = kCommon;
  out += "void main() {\n  vec2 uv = vPos;\n  float m = uParams[0].x;\n";
  out += "  vec4 a = texture(uTex0, uv), c = texture(uTex1, uv), r = c;\n";
  const auto& modes = schema::transitionModes();
  for (size_t i = 0; i < modes.size(); ++i)
    out += "  " + std::string(i ? "else " : "") + "if (uMode == " + std::to_string(i) + ") { " + modes[i].glsl + " }\n";
  out += "  color = r;\n}\n";
  return out;
}

std::string Renderer::effectShader(const schema::EffectType& t) {
  if (t.glsl.rfind("//ISF\n", 0) == 0) {
    return std::string(R"(#version 410 core
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform vec4 uParams[4];
in vec2 vPos;
out vec4 vjOut;
#define gl_FragColor vjOut
#define isf_FragNormCoord vPos
#define RENDERSIZE uParams[3].xy
#define TIME uParams[3].z
#define PASSINDEX 0
#define IMG_THIS_PIXEL(i) texture(i, vPos)
#define IMG_THIS_NORM_PIXEL(i) texture(i, vPos)
#define IMG_NORM_PIXEL(i, c) texture(i, c)
#define IMG_PIXEL(i, c) texture(i, (c) / RENDERSIZE)
#define IMG_SIZE(i) vec2(textureSize(i, 0))
#define texture2D texture
)") + t.glsl.substr(6);
  }
  std::string out = kCommon;
  out += R"(
#define uSize uParams[3].xy
#define uTime uParams[3].z
#define uBeat uParams[3].w
#define P(i) uParams[(i) / 4][(i) % 4]
uniform sampler2D uTex2;
vec4 src(vec2 uv) { return texture(uTex0, uv); }
vec4 mem(vec2 uv) { return texture(uTex1, uv); }
vec4 mem2(vec2 uv) { return texture(uTex2, uv); }
vec3 unpre(vec4 c) { return c.a > 0.0 ? c.rgb / c.a : vec3(0.0); }
vec4 pre(vec3 k, float a) { return vec4(clamp(k, 0.0, 1.0) * a, a); }
vec4 fx(vec2 uv) {
)";
  out += t.glsl;
  out += "\n}\nvoid main() { color = fx(vPos); }\n";
  return out;
}

gfx::ProgramId Renderer::programFor(const schema::EffectType& t) {
  auto it = effectPrograms_.find(t.id);
  if (it != effectPrograms_.end()) return it->second;
  gfx::ProgramId id = 0;
  try {
    id = gpu_.createProgram(effectShader(t));
  } catch (const std::exception& e) {
    effectErrors_[t.id] = e.what();
  }
  effectPrograms_[t.id] = id;
  return id;
}

Renderer::EffectMemory& Renderer::memoryFor(const PlanEffect& e) {
  EffectMemory& m = memory_[e.instance];
  m.used = true;
  if (m.ring.empty()) {
    m.ring.reserve(static_cast<size_t>(e.type->memoryFrames));
    for (int i = 0; i < e.type->memoryFrames; ++i) m.ring.push_back(gpu_.createTarget(width_, height_));
  }
  return m;
}

void Renderer::remember(EffectMemory& m, gfx::TargetId source) {
  m.head = (m.head + 1) % m.ring.size();
  gpu_.beginPass(m.ring[m.head], true);
  gfx::DrawCall copy;
  copy.program = copyProgram_;
  copy.textures[0] = gpu_.targetTexture(source);
  gpu_.draw(copy);
  m.filled = std::min(m.filled + 1, static_cast<int>(m.ring.size()));
}

// Antes de reunir suficientes fotogramas devuelve el más antiguo que haya.
gfx::TextureId Renderer::recall(const EffectMemory& m, int age) {
  if (m.filled == 0) return emptyTexture_;
  const int back = std::clamp(age, 0, m.filled - 1), size = static_cast<int>(m.ring.size());
  return gpu_.targetTexture(m.ring[static_cast<size_t>((static_cast<int>(m.head) + size - back) % size)]);
}

void Renderer::releaseMemory() {
  for (auto& [_, m] : memory_)
    for (auto id : m.ring) gpu_.destroyTarget(id);
  memory_.clear();
}

void Renderer::applyChain(const PlanChain& chain, gfx::TargetId target) {
  // Cada efecto lee el resultado del anterior y se mezcla con él según su opacidad y blend.
  for (const PlanEffect& e : chain) {
    const bool patch = e.type->glsl.rfind("//PATCH", 0) == 0;
    const bool external = patch || e.type->glsl.rfind("//FFGL", 0) == 0;
    if (external) {
      IExternalEffects* runner = patch ? patches_ : external_;
      // Un plugin que falla no aporta nada: la imagen sigue como estaba.
      if (!runner || !runner->render(e, gpu_, target, targets_[kFx0], width_, height_, time_, bpm_, beat_)) {
        effectErrors_[e.type->id] = "el plugin no se pudo ejecutar";
        continue;
      }
    } else {
    const gfx::ProgramId program = programFor(*e.type);
    if (!program) continue;
    EffectMemory* memory = e.type->memoryFrames > 0 ? &memoryFor(e) : nullptr;
    if (memory && !e.type->memoryOutput) remember(*memory, target);
    gpu_.beginPass(targets_[kFx0], true);
    gfx::DrawCall d;
    d.program = program;
    d.textures[0] = gpu_.targetTexture(target);
    d.textures[1] = emptyTexture_;
    if (e.aux) {
      auto [it, fresh] = auxTextures_.try_emplace(e.aux.get());
      if (fresh) it->second = {gpu_.uploadTexture(gfx::kNoTexture, e.aux->width, e.aux->height, e.aux->format, e.aux->stride, e.aux->pixels.data()), e.aux};
      d.textures[1] = it->second.texture;
    }
    if (memory) {
      const auto age = [&](int param) { return param >= 0 ? static_cast<int>(e.values[static_cast<size_t>(param)] + 0.5f) : 0; };
      // Sin nada guardado aún se usa la imagen actual: el efecto nunca arranca en negro.
      const gfx::TextureId now = gpu_.targetTexture(target);
      d.textures[1] = memory->filled ? recall(*memory, age(e.type->ageA)) : now;
      d.textures[2] = memory->filled ? recall(*memory, age(e.type->ageB)) : now;
    }
    std::copy(e.values.begin(), e.values.end(), d.params.begin());
    d.params[12] = static_cast<float>(width_);
    d.params[13] = static_cast<float>(height_);
    d.params[14] = time_;
    d.params[15] = beat_;
    gpu_.draw(d);
    if (memory && e.type->memoryOutput) remember(*memory, targets_[kFx0]);
    }

    gpu_.beginPass(targets_[kFx1], false);
    // Con Alpha el efecto sustituye al original según su opacidad: "encima" dejaría ver
    // el original a través de lo que el efecto haya vuelto transparente.
    gfx::DrawCall mix;
    mix.program = e.blend == alphaMode_ ? mixProgram_ : blendProgram_;
    mix.textures[0] = gpu_.targetTexture(target);
    mix.textures[1] = gpu_.targetTexture(targets_[kFx0]);
    mix.params[0] = e.opacity;
    mix.mode = e.blend;
    gpu_.draw(mix);

    gpu_.beginPass(target, false);
    gfx::DrawCall copy;
    copy.program = copyProgram_;
    copy.textures[0] = gpu_.targetTexture(targets_[kFx1]);
    gpu_.draw(copy);
  }
}

Renderer::Renderer(gfx::IRenderBackend& backend) : gpu_(backend) {
  clipProgram_ = gpu_.createProgram(std::string(kCommon) + kClipFs);
  maskProgram_ = gpu_.createProgram(std::string(kCommon) + kMaskFs);
  blendProgram_ = gpu_.createProgram(blendShader());
  transitionProgram_ = gpu_.createProgram(transitionShader());
  copyProgram_ = gpu_.createProgram(std::string(kCommon) + kCopyFs);
  mixProgram_ = gpu_.createProgram(std::string(kCommon) + kMixFs);
  const uint8_t none[4] = {0, 0, 0, 0};
  emptyTexture_ = gpu_.uploadTexture(gfx::kNoTexture, 1, 1, gfx::PixelFormat::RGBA8, 4, none);
  const auto& modes = schema::blendModes();
  for (size_t i = 0; i < modes.size(); ++i)
    if (std::string(modes[i].name) == "Alpha") alphaMode_ = static_cast<int>(i);
}

gfx::TargetId Renderer::downscale(gfx::TargetId source, int width, int height) {
  if (width != monitorW_ || height != monitorH_) {
    if (monitor_) gpu_.destroyTarget(monitor_);
    monitor_ = gpu_.createTarget(width, height);
    monitorW_ = width;
    monitorH_ = height;
  }
  gpu_.beginPass(monitor_, true);
  gfx::DrawCall d;
  d.program = clipProgram_;
  d.textures[0] = gpu_.targetTexture(source);
  d.params = {1, 1, 1, 1, 1, 0};
  d.flipV = true;
  gpu_.draw(d);
  return monitor_;
}

Renderer::~Renderer() {
  if (monitor_) gpu_.destroyTarget(monitor_);
  for (auto* pool : {&routedLayers_, &routedGroups_})
    for (auto& [_, id] : *pool) gpu_.destroyTarget(id);
  for (auto id : targets_)
    if (id) gpu_.destroyTarget(id);
  releaseMemory();
  for (auto* pool : {busTargets_, busScratch_})
    for (int i = 0; i < 2; ++i)
      if (pool[i]) gpu_.destroyTarget(pool[i]);
  for (auto& [_, c] : textures_) gpu_.destroyTexture(c.texture);
  for (auto& [_, t] : auxTextures_) gpu_.destroyTexture(t.texture);
  gpu_.destroyTexture(emptyTexture_);
}

void Renderer::ensureTargets(int width, int height) {
  if (width == width_ && height == height_) return;
  for (auto* pool : {&routedLayers_, &routedGroups_}) {
    for (auto& [_, id] : *pool) gpu_.destroyTarget(id);
    pool->clear();
  }
  releaseMemory();
  for (auto* pool : {busTargets_, busScratch_})
    for (int i = 0; i < 2; ++i) {
      if (pool[i]) gpu_.destroyTarget(pool[i]);
      pool[i] = 0;
    }
  for (auto& id : targets_) {
    if (id) gpu_.destroyTarget(id);
    id = gpu_.createTarget(width, height);
  }
  width_ = width;
  height_ = height;
}

gfx::TextureId Renderer::textureFor(const PlanClip& clip) {
  Cached& c = textures_[clip.key];
  c.used = true;
  if (c.frame != clip.frame.get()) {
    const auto& f = *clip.frame;
    c.texture = gpu_.uploadTexture(c.texture, f.width, f.height, f.format, f.stride, f.pixels.data());
    c.frame = clip.frame.get();
    c.hold = clip.frame;  // mantiene viva la dirección con la que se compara
  }
  return c.texture;
}

void Renderer::drawClip(const PlanClip& clip, gfx::TargetId into, int w, int h) {
  gpu_.beginPass(into, true);
  if (!clip.frame || clip.hidden) return;
  double cw = 0, ch = 0;
  resizedSize(clip.resize, clip.frame->width, clip.frame->height, w, h, cw, ch);
  gfx::DrawCall d;
  d.program = clipProgram_;
  d.textures[0] = textureFor(clip);
  d.matrix = transformMatrix(clip.transform, cw, ch, w, h);
  d.params = {clip.channels[0] ? 1.f : 0.f, clip.channels[1] ? 1.f : 0.f, clip.channels[2] ? 1.f : 0.f,
              clip.useAlpha ? 1.f : 0.f, clip.opacity, clip.straightAlpha ? 1.f : 0.f};
  gpu_.draw(d);
  applyChain(clip.effects, into);
}

void Renderer::transformInto(gfx::TargetId from, gfx::TargetId into, const TransformValues& t, int w, int h) {
  gpu_.beginPass(into, true);
  gfx::DrawCall d;
  d.program = clipProgram_;
  d.textures[0] = gpu_.targetTexture(from);
  d.matrix = transformMatrix(t, w, h, w, h);
  d.params = {1, 1, 1, 1, 1, 0};
  d.flipV = true;
  gpu_.draw(d);
}

void Renderer::renderLayer(const PlanLayer& layer, gfx::TargetId into, int w, int h) {
  if (layer.previous) {
    drawClip(*layer.previous, targets_[kClipA], w, h);
    if (layer.current) drawClip(*layer.current, targets_[kClipB], w, h);
    else gpu_.beginPass(targets_[kClipB], true);
    gpu_.beginPass(into, false);
    gfx::DrawCall d;
    d.program = transitionProgram_;
    d.textures[0] = gpu_.targetTexture(targets_[kClipA]);
    d.textures[1] = gpu_.targetTexture(targets_[kClipB]);
    d.params[0] = layer.mix;
    d.mode = layer.transition;
    gpu_.draw(d);
  } else if (layer.current) {
    drawClip(*layer.current, into, w, h);
  } else {
    gpu_.beginPass(into, true);
  }
}

void Renderer::blendInto(gfx::TargetId& accum, gfx::TargetId& scratch, gfx::TargetId source, int mode, float opacity) {
  gpu_.beginPass(scratch, false);
  gfx::DrawCall d;
  d.program = blendProgram_;
  d.textures[0] = gpu_.targetTexture(accum);
  d.textures[1] = gpu_.targetTexture(source);
  d.params[0] = opacity;
  d.mode = mode;
  gpu_.draw(d);
  std::swap(accum, scratch);
}

void Renderer::maskInto(gfx::TargetId& accum, gfx::TargetId& scratch, gfx::TargetId mask, float amount) {
  gpu_.beginPass(scratch, false);
  gfx::DrawCall d;
  d.program = maskProgram_;
  d.textures[0] = gpu_.targetTexture(accum);
  d.textures[1] = gpu_.targetTexture(mask);
  d.params[0] = amount;
  gpu_.draw(d);
  std::swap(accum, scratch);
}

gfx::TargetId Renderer::routeTarget(std::map<int, gfx::TargetId>& pool, int key) {
  auto& id = pool[key];
  if (!id) id = gpu_.createTarget(width_, height_);
  return id;
}

void Renderer::copyInto(gfx::TargetId from, gfx::TargetId to) {
  gpu_.beginPass(to, true);
  gfx::DrawCall d;
  d.program = copyProgram_;
  d.textures[0] = gpu_.targetTexture(from);
  gpu_.draw(d);
}

gfx::TextureId Renderer::routedLayer(int layer) {
  auto it = routedLayers_.find(layer);
  return it == routedLayers_.end() ? gfx::kNoTexture : gpu_.targetTexture(it->second);
}

gfx::TextureId Renderer::routedBus(int bus) {
  if (bus < 1 || bus > 2 || !busTargets_[bus - 1]) return gfx::kNoTexture;
  return gpu_.targetTexture(busTargets_[bus - 1]);
}

gfx::TextureId Renderer::routedGroup(int group) {
  auto it = routedGroups_.find(group);
  return it == routedGroups_.end() ? gfx::kNoTexture : gpu_.targetTexture(it->second);
}

gfx::TargetId Renderer::render(const RenderPlan& plan) {
  const int w = plan.width, h = plan.height;
  ensureTargets(w, h);
  time_ = plan.time;
  beat_ = plan.beat;
  bpm_ = plan.bpm;
  for (auto& [_, c] : textures_) c.used = false;
  for (auto& [_, m] : memory_) m.used = false;

  gfx::TargetId accum = targets_[kAccum], accumB = targets_[kAccumB];
  gfx::TargetId group = targets_[kGroup], groupB = targets_[kGroupB];
  gpu_.beginPass(accum, true);
  for (int b = 0; b < 2; ++b) {
    if (!plan.routeBus[b]) continue;
    if (!busTargets_[b]) {
      busTargets_[b] = gpu_.createTarget(w, h);
      busScratch_[b] = gpu_.createTarget(w, h);
    }
    gpu_.beginPass(busTargets_[b], true);
  }
  if (plan.bypass) return accum;

  const auto hasContent = [](const PlanLayer& l) { return l.visible && (l.current || l.previous); };
  const size_t n = plan.layers.size();

  for (size_t i = 0; i < n;) {
    const int g = plan.layers[i].group;
    size_t end = i + 1;
    while (g > 0 && end < n && plan.layers[end].group == g) ++end;

    // Un grupo arranca sobre negro transparente y se aplana antes de mezclarse.
    gfx::TargetId* dst = g > 0 ? &group : &accum;
    gfx::TargetId* dstB = g > 0 ? &groupB : &accumB;
    if (g > 0) gpu_.beginPass(group, true);

    for (size_t k = i; k < end; ++k) {
      const PlanLayer& layer = plan.layers[k];
      if (layer.routed) {
        // Se limpia aunque no suene nada: el slice no debe conservar la última imagen.
        gpu_.beginPass(routeTarget(routedLayers_, static_cast<int>(k) + 1), true);
      }
      if (!hasContent(layer)) continue;
      gfx::TargetId src = targets_[kLayer];
      renderLayer(layer, src, w, h);
      applyChain(layer.effects, src);
      if (!layer.transform.identity()) {
        transformInto(src, targets_[kLayerB], layer.transform, w, h);
        src = targets_[kLayerB];
      }
      if (layer.routed) {
        copyInto(src, routeTarget(routedLayers_, static_cast<int>(k) + 1));
        continue;  // sale de la mezcla: blend y efectos de composición no le aplican
      }
      if (layer.mask == MaskMode::AllBelow) {
        maskInto(*dst, *dstB, src, layer.opacity);
        continue;
      }
      if (layer.mask == MaskMode::OneBelow) continue;  // actúa al pintar la capa de debajo
      const bool maskAbove = k + 1 < n && plan.layers[k + 1].group == g && plan.layers[k + 1].mask == MaskMode::OneBelow &&
                             hasContent(plan.layers[k + 1]);
      if (maskAbove) {
        const PlanLayer& upper = plan.layers[k + 1];
        renderLayer(upper, targets_[kMask], w, h);
        gfx::TargetId masked = src, scratch = src == targets_[kLayer] ? targets_[kLayerB] : targets_[kLayer];
        maskInto(masked, scratch, targets_[kMask], upper.opacity);
        src = masked;
      }
      // Las salidas por bus ven su lado completo, sea cual sea la posición del crossfader.
      if (layer.bus > 0 && plan.routeBus[layer.bus - 1])
        blendInto(busTargets_[layer.bus - 1], busScratch_[layer.bus - 1], src, layer.blend, layer.busOpacity);
      blendInto(*dst, *dstB, src, layer.blend, layer.opacity);
    }

    if (g > 0 && g <= static_cast<int>(plan.groups.size()) && plan.groups[g - 1].visible) {
      const PlanGroup& pg = plan.groups[g - 1];
      applyChain(pg.effects, group);
      if (pg.routed) {
        copyInto(group, routeTarget(routedGroups_, g));
        i = end;
        continue;
      }
      gfx::TargetId src = group;
      if (!pg.transform.identity()) {
        transformInto(group, groupB, pg.transform, w, h);
        src = groupB;
      }
      blendInto(accum, accumB, src, pg.blend, pg.opacity);
    }
    i = end;
  }

  applyChain(plan.effects, accum);

  if (plan.master < 1) {
    // El maestro atenúa el resultado: se mezcla con negro transparente.
    gpu_.beginPass(accumB, false);
    gfx::DrawCall d;
    d.program = clipProgram_;
    d.textures[0] = gpu_.targetTexture(accum);
    d.params = {1, 1, 1, 1, plan.master, 0};
    d.flipV = true;
    gpu_.draw(d);
    std::swap(accum, accumB);
  }

  // Un efecto que deja de aplicarse suelta su memoria y, al volver, empieza limpio.
  for (auto it = memory_.begin(); it != memory_.end();) {
    if (it->second.used) {
      ++it;
    } else {
      for (auto id : it->second.ring) gpu_.destroyTarget(id);
      it = memory_.erase(it);
    }
  }

  // Libera las texturas de clips que ya no se dibujan.
  for (auto it = textures_.begin(); it != textures_.end();) {
    if (it->second.used) {
      ++it;
    } else {
      gpu_.destroyTexture(it->second.texture);
      it = textures_.erase(it);
    }
  }
  return accum;
}

}  // namespace vj::compositor
