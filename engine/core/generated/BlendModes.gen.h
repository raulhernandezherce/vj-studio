// Generado por tools/gen_schema.py desde schemas/. No editar.
#pragma once
#include <vector>

namespace vj::schema {

struct BlendMode {
  const char* name;
  const char* blend;  // función de mezcla, o nullptr
  const char* raw;    // cuerpo completo, o nullptr
};

struct TransitionMode {
  const char* name;
  const char* glsl;
};

inline const std::vector<BlendMode>& blendModes() {
  static const std::vector<BlendMode> table = {
    {"Add", R"(min(cb + cs, vec3(1.0)))", nullptr},
    {"Alpha", R"(cs)", nullptr},
    {"Cut", nullptr, R"(r = o >= 0.5 ? s : b;)"},
    {"Darken", R"(min(cb, cs))", nullptr},
    {"Multiply", R"(cb * cs)", nullptr},
    {"Burn", R"(burn(cb, cs))", nullptr},
    {"Lighten", R"(max(cb, cs))", nullptr},
    {"Screen", R"(1.0 - (1.0 - cb) * (1.0 - cs))", nullptr},
    {"Dodge", R"(dodge(cb, cs))", nullptr},
    {"Overlay", R"(hardlight(cs, cb))", nullptr},
    {"Soft Light", R"(softlight(cb, cs))", nullptr},
    {"Hard Light", R"(hardlight(cb, cs))", nullptr},
    {"Subtract", R"(max(cb - cs, vec3(0.0)))", nullptr},
    {"Difference", R"(abs(cb - cs))", nullptr},
    {"Difference I", R"(1.0 - abs(cb - cs))", nullptr},
    {"Exclusion", R"(cb + cs - 2.0 * cb * cs)", nullptr},
    {"Luma Key", nullptr, R"(r = over(b, s * smoothstep(0.1, 0.4, l) * o);)"},
    {"Luma Key I", nullptr, R"(r = over(b, s * (1.0 - smoothstep(0.1, 0.4, l)) * o);)"},
    {"Luma is Alpha", nullptr, R"(r = over(b, vec4(cs * l * s.a, l * s.a) * o);)"},
    {"Displace", nullptr, R"(r = texture(uTex0, vPos + vec2((l - 0.5) * o * 0.2, 0.0));)"},
    {"50Mask", nullptr, R"(r = mix(b, b * l * s.a, o);)"},
    {"RGB", nullptr, R"(vec4 f = over(b, s); vec3 m = clamp(vec3(o * 3.0, o * 3.0 - 1.0, o * 3.0 - 2.0), 0.0, 1.0); r = vec4(mix(b.rgb, f.rgb, m), mix(b.a, f.a, o));)"},
  };
  return table;
}

inline const std::vector<TransitionMode>& transitionModes() {
  static const std::vector<TransitionMode> table = {
    {"Alpha", R"(r = mix(a, c, m);)"},
    {"Cut", R"(r = m >= 0.5 ? c : a;)"},
    {"Wipe Left", R"(r = uv.x >= 1.0 - m ? c : a;)"},
    {"Wipe Up", R"(r = uv.y <= m ? c : a;)"},
    {"Wipe Ellipse", R"(r = length((uv - 0.5) * vec2(1.0, 1.0)) <= m * 0.7072 ? c : a;)"},
    {"Dissolve", R"(r = hash(floor(uv * vec2(240.0, 135.0))) < m ? c : a;)"},
    {"Push Up", R"(r = uv.y >= 1.0 - m ? texture(uTex1, vec2(uv.x, uv.y - 1.0 + m)) : texture(uTex0, vec2(uv.x, uv.y + m));)"},
  };
  return table;
}

}  // namespace vj::schema
