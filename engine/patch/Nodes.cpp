#include <algorithm>
#include <cmath>

#include "core/generated/BlendModes.gen.h"
#include "patch/Patch.h"

using nlohmann::json;

namespace vj::patch {
namespace {

Port num(std::string name, double def = 0, double min = 0, double max = 1) { return {std::move(name), PortType::Float, def, min, max, ""}; }
Port integer(std::string name, double def = 0, double min = 0, double max = 100) { return {std::move(name), PortType::Int, def, min, max, ""}; }
Port flag(std::string name) { return {std::move(name), PortType::Bool, 0, 0, 1, ""}; }
Port trig(std::string name) { return {std::move(name), PortType::Trigger, 0, 0, 1, ""}; }
Port str(std::string name, std::string def = "") { return {std::move(name), PortType::String, 0, 0, 0, std::move(def)}; }
Port col(std::string name) { return {std::move(name), PortType::Color, 0, 0, 1, ""}; }
Port tex(std::string name) { return {std::move(name), PortType::Texture, 0, 0, 0, ""}; }

Attr attrSize(const char* name, const char* label) { return {name, label, "int", 0, {}}; }

std::vector<Attr> textureAttrs() {
  return {attrSize("width", "Ancho (0: el del programa)"), attrSize("height", "Alto (0: el del programa)"),
          {"depth", "Profundidad de bits", "enum", "8", {"8", "16"}}};
}

std::vector<std::string> blendNames() {
  std::vector<std::string> out;
  for (const auto& m : schema::blendModes()) out.push_back(m.name);
  return out;
}
std::vector<std::string> transitionNames() {
  std::vector<std::string> out;
  for (const auto& m : schema::transitionModes()) out.push_back(m.name);
  return out;
}

using Eval = std::function<void(EvalContext&)>;

NodeType data(const char* id, const char* name, const char* category, const char* description, std::vector<Port> in, std::vector<Port> out, Eval eval, std::vector<Attr> attrs = {}) {
  NodeType t;
  t.id = id;
  t.name = name;
  t.category = category;
  t.description = description;
  t.inputs = std::move(in);
  t.outputs = std::move(out);
  t.eval = std::move(eval);
  t.attrs = std::move(attrs);
  return t;
}

NodeType math1(const char* id, const char* name, const char* description, double (*fn)(double)) {
  return data(id, name, "Matemáticas", description, {num("a")}, {num("out")}, [fn](EvalContext& c) { c.out[0].n = fn(c.in[0].n); });
}
NodeType math2(const char* id, const char* name, const char* description, double (*fn)(double, double), double defB = 0) {
  return data(id, name, "Matemáticas", description, {num("a"), num("b", defB)}, {num("out")}, [fn](EvalContext& c) { c.out[0].n = fn(c.in[0].n, c.in[1].n); });
}
NodeType compare(const char* id, const char* name, const char* description, bool (*fn)(double, double)) {
  return data(id, name, "Comparación", description, {num("a"), num("b")}, {flag("out")}, [fn](EvalContext& c) { c.out[0].n = fn(c.in[0].n, c.in[1].n); });
}

NodeType texture(const char* id, const char* name, const char* category, const char* description, Kind kind, std::vector<Port> in, std::vector<Attr> extra = {}, bool sized = true) {
  NodeType t;
  t.id = id;
  t.name = name;
  t.category = category;
  t.description = description;
  t.kind = kind;
  t.inputs = std::move(in);
  t.outputs = {tex("out")};
  t.attrs = std::move(extra);
  if (sized)
    for (Attr& a : textureAttrs()) t.attrs.push_back(std::move(a));
  return t;
}

std::string text(const Value& v) { return v.s; }

}  // namespace

NodeLibrary::NodeLibrary(const compositor::EffectCatalog& catalog) {
  auto& t = types_;
  const std::vector<Attr> range = {{"min", "Mínimo", "float", 0, {}}, {"max", "Máximo (igual al mínimo: sin deslizador)", "float", 0, {}}};

  t.push_back(data("data.float", "Float", "Interfaz", "Número decimal. Con mínimo y máximo crea un deslizador en el programa.", {num("value", 0, -1e6, 1e6)}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n; }, range));
  t.push_back(data("data.int", "Integer", "Interfaz", "Número entero. Con mínimo y máximo crea un deslizador.", {integer("value", 0, -1000000, 1000000)}, {integer("out")}, [](EvalContext& c) { c.out[0].n = std::round(c.in[0].n); }, range));
  t.push_back(data("data.bool", "Boolean", "Interfaz", "Verdadero o falso.", {flag("value")}, {flag("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0.5; }));
  t.push_back(data("data.trigger", "Trigger", "Interfaz", "Botón en el programa.", {trig("value")}, {trig("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0.5; }));
  t.push_back(data("data.color", "Color", "Interfaz", "Color con selector.", {col("value")}, {col("out")}, [](EvalContext& c) { c.out[0].c = c.in[0].c; }));
  t.push_back(data("data.string", "String", "Texto", "Texto fijo.", {str("value")}, {str("out")}, [](EvalContext& c) { c.out[0].s = c.in[0].s; }));
  t.push_back(data("text.concat", "Concatenate", "Texto", "Une dos textos.", {str("a"), str("b")}, {str("out")}, [](EvalContext& c) { c.out[0].s = text(c.in[0]) + text(c.in[1]); }));
  t.push_back(data("text.substring", "Substring", "Texto", "Parte de un texto.", {str("text"), integer("start", 0), integer("length", 10)}, {str("out")}, [](EvalContext& c) {
    const auto& s = c.in[0].s;
    const size_t from = std::min<size_t>(s.size(), static_cast<size_t>(std::max(0.0, c.in[1].n)));
    c.out[0].s = s.substr(from, static_cast<size_t>(std::max(0.0, c.in[2].n)));
  }));
  t.push_back(data("text.length", "Length", "Texto", "Longitud de un texto.", {str("text")}, {integer("out")}, [](EvalContext& c) { c.out[0].n = static_cast<double>(c.in[0].s.size()); }));

  t.push_back(data("in.time", "Time", "Entrada", "Segundos desde el inicio.", {}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.time; }));
  t.push_back(data("in.beat", "Beat", "Entrada", "Pulso del programa y su fase de 0 a 1.", {}, {num("beat"), num("phase")}, [](EvalContext& c) {
    c.out[0].n = c.beat;
    c.out[1].n = c.beat - std::floor(c.beat);
  }));
  t.push_back(data("in.bpm", "BPM", "Entrada", "Tempo del programa.", {}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.bpm; }));
  t.push_back(data("in.level", "Audio Level", "Entrada", "Nivel de audio izquierdo y derecho.", {}, {num("left"), num("right")}, [](EvalContext& c) {
    c.out[0].n = c.params->getFloat("/audio/level/left");
    c.out[1].n = c.params->getFloat("/audio/level/right");
  }));
  t.push_back(data("in.param", "Parameter", "Entrada", "Lee cualquier parámetro del programa: capas, clips, audio, OSC o MIDI ya mapeados.", {str("path", "/composition/master")}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.params->getFloat(c.in[0].s); }));
  t.push_back(data("out.param", "Set Parameter", "Salida", "Escribe un parámetro cuando el valor cambia.", {str("path", "/composition/master"), num("value")}, {}, [](EvalContext& c) {
    if (c.state[1] != 0 && c.state[0] == c.in[1].n) return;
    c.state[0] = c.in[1].n;
    c.state[1] = 1;
    if (c.params->set(c.in[0].s, c.in[1].n) != ParamError::None) *c.error = "no se pudo escribir " + c.in[0].s;
  }));
  t.push_back(data("out.trigger", "Fire Parameter", "Salida", "Dispara un parámetro en cada flanco: por ejemplo, el connect de un clip.", {str("path", "/composition/layers/1/clips/1/connect"), trig("fire")}, {}, [](EvalContext& c) {
    const bool high = c.in[1].n > 0.5;
    if (high && c.state[0] == 0 && c.params->trigger(c.in[0].s) != ParamError::None) *c.error = "no se pudo disparar " + c.in[0].s;
    c.state[0] = high;
  }));

  t.push_back(math2("math.add", "Add", "a + b", [](double a, double b) { return a + b; }));
  t.push_back(math2("math.sub", "Subtract", "a - b", [](double a, double b) { return a - b; }));
  t.push_back(math2("math.mul", "Multiply", "a × b", [](double a, double b) { return a * b; }, 1));
  t.push_back(math2("math.div", "Divide", "a / b; 0 si b es 0", [](double a, double b) { return b == 0 ? 0 : a / b; }, 1));
  t.push_back(math2("math.mod", "Modulo", "Resto positivo de a / b", [](double a, double b) { return b == 0 ? 0 : a - b * std::floor(a / b); }, 1));
  t.push_back(math2("math.min", "Min", "El menor", [](double a, double b) { return std::min(a, b); }));
  t.push_back(math2("math.max", "Max", "El mayor", [](double a, double b) { return std::max(a, b); }));
  t.push_back(math2("math.pow", "Power", "a elevado a b", [](double a, double b) { return std::pow(a, b); }, 1));
  t.push_back(math1("math.abs", "Abs", "Valor absoluto", [](double a) { return std::fabs(a); }));
  t.push_back(math1("math.sin", "Sine", "Seno de a vueltas: de -1 a 1", [](double a) { return std::sin(a * 6.283185307179586); }));
  t.push_back(math1("math.cos", "Cosine", "Coseno de a vueltas", [](double a) { return std::cos(a * 6.283185307179586); }));
  t.push_back(math1("math.round", "Round", "Entero más cercano", [](double a) { return std::round(a); }));
  t.push_back(data("math.lerp", "Lerp", "Matemáticas", "Mezcla a y b según t.", {num("a"), num("b", 1), num("t")}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n + (c.in[1].n - c.in[0].n) * c.in[2].n; }));
  t.push_back(data("math.clamp", "Clamp", "Matemáticas", "Limita a un rango.", {num("a"), num("min"), num("max", 1)}, {num("out")}, [](EvalContext& c) { c.out[0].n = std::clamp(c.in[0].n, c.in[1].n, std::max(c.in[1].n, c.in[2].n)); }));
  t.push_back(data("math.map", "Map Range", "Matemáticas", "Lleva un valor de un rango a otro.", {num("a"), num("fromMin"), num("fromMax", 1), num("toMin"), num("toMax", 1)}, {num("out")}, [](EvalContext& c) {
    const double span = c.in[2].n - c.in[1].n;
    c.out[0].n = span == 0 ? c.in[3].n : c.in[3].n + (c.in[0].n - c.in[1].n) / span * (c.in[4].n - c.in[3].n);
  }));
  t.push_back(data("math.falloff", "Falloff", "Matemáticas", "Sube al instante y cae despacio: suaviza el espectro.", {num("in"), num("release", 0.3, 0.001, 10)}, {num("out")}, [](EvalContext& c) {
    c.state[0] = std::max(c.in[0].n, c.state[0] * std::exp(-c.dt / std::max(0.001, c.in[1].n)));
    c.out[0].n = c.state[0];
  }));
  t.push_back(data("math.freq-pitch", "Frequency to Pitch", "Matemáticas", "Hercios a nota MIDI.", {num("hz", 440, 1, 20000)}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0 ? 69 + 12 * std::log2(c.in[0].n / 440) : 0; }));
  t.push_back(data("math.freq-bpm", "Frequency to BPM", "Matemáticas", "Hercios a pulsos por minuto.", {num("hz", 2, 0, 100)}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n * 60; }));

  t.push_back(compare("cmp.gt", "Greater", "a > b", [](double a, double b) { return a > b; }));
  t.push_back(compare("cmp.lt", "Less", "a < b", [](double a, double b) { return a < b; }));
  t.push_back(compare("cmp.eq", "Equal", "a = b", [](double a, double b) { return std::fabs(a - b) < 1e-9; }));
  t.push_back(data("cmp.and", "And", "Comparación", "Las dos entradas.", {flag("a"), flag("b")}, {flag("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0.5 && c.in[1].n > 0.5; }));
  t.push_back(data("cmp.or", "Or", "Comparación", "Alguna entrada.", {flag("a"), flag("b")}, {flag("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0.5 || c.in[1].n > 0.5; }));
  t.push_back(data("cmp.not", "Not", "Comparación", "Lo contrario.", {flag("a")}, {flag("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n <= 0.5; }));
  t.push_back(data("cmp.select", "Select", "Comparación", "a si la condición es cierta; si no, b.", {flag("if"), num("a"), num("b")}, {num("out")}, [](EvalContext& c) { c.out[0].n = c.in[0].n > 0.5 ? c.in[1].n : c.in[2].n; }));
  t.push_back(data("seq.steps", "Step Sequencer", "Secuencia", "Recorre un patrón de pasos (por ejemplo 1000) al ritmo del pulso y emite un trigger en cada 1.", {str("pattern", "1000"), num("stepsPerBeat", 1, 0.25, 16)}, {trig("gate"), integer("step")}, [](EvalContext& c) {
    const auto& p = c.in[0].s;
    if (p.empty()) return;
    const double pos = c.beat * std::max(0.01, c.in[1].n);
    const size_t step = static_cast<size_t>(std::floor(pos)) % p.size();
    c.out[0].n = p[step] == '1' && pos - std::floor(pos) < 0.5;
    c.out[1].n = static_cast<double>(step);
  }));

  t.push_back(texture("tex.in", "Texture Input", "Entrada", "Imagen que entrega el programa: la de la capa o el clip donde esté el efecto.", Kind::TextureIn, {}, {}, false));
  t.back().outputs = {tex("out")};
  t.push_back(texture("tex.out", "Texture Output", "Salida", "Imagen que el parche devuelve al programa.", Kind::TextureOut, {tex("in")}, {}, false));
  t.back().outputs = {};
  t.push_back(texture("tex.blend", "Texture Material", "Textura", "Mezcla dos texturas con un blend mode.", Kind::Blend, {tex("base"), tex("top"), num("opacity", 1)}, {{"mode", "Blend mode", "enum", "Alpha", blendNames()}}));
  t.push_back(texture("tex.mixer", "Video Mixer", "Textura", "Fundido entre dos texturas.", Kind::Mixer, {tex("a"), tex("b"), num("mix")}));
  t.push_back(texture("tex.transition", "Transition", "Textura", "Transición entre dos texturas.", Kind::Transition, {tex("a"), tex("b"), num("progress")}, {{"mode", "Transición", "enum", transitionNames().empty() ? "" : transitionNames()[0], transitionNames()}}));
  t.push_back(texture("tex.solid", "Solid Color", "Textura", "Color liso.", Kind::Solid, {col("color")}));
  t.push_back(texture("tex.shape", "Rectangle", "Formas", "Cuadrado. Con Instancias se repite en rejilla.", Kind::Shape, {col("color"), num("size", 0.3, 0, 2), num("rotation", 0, -1, 1), num("spread", 1, 0, 2)}, {{"instances", "Instancias", "int", 1, {}}}));
  t.push_back(texture("tex.isf", "ISF Shader", "Textura", "Shader ISF escrito aquí.", Kind::Isf, {tex("in")}, {{"code", "Código ISF", "code",
    "/*{ \"INPUTS\": [] }*/\nvoid main() {\n  vec2 uv = isf_FragNormCoord;\n  gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(TIME), 1.0);\n}\n", {}}}));
  t.push_back(texture("tex.media", "Media", "Textura", "Vídeo o imagen de un archivo. Consolidar copia el archivo junto al parche.", Kind::Media, {}, {{"path", "Archivo", "file", "", {}}}));

  for (const schema::EffectType& e : schema::effectTypes()) {
    const auto* found = catalog.find(e.id);
    if (!found) continue;
    if (std::any_of(found->params.begin(), found->params.end(), [](const auto& q) { return q.type == "string"; })) continue;
    NodeType n = texture(("fx." + e.id).c_str(), found->name.c_str(), found->source ? "Fuentes" : "Efectos", found->category.c_str(), Kind::Effect, {});
    if (!found->source) n.inputs.push_back(tex("in"));
    for (const auto& q : found->params) n.inputs.push_back(q.type == "bool" ? flag(q.name) : num(q.name, q.defaultValue, q.min, q.max));
    for (Port& q : n.inputs)
      for (const auto& fp : found->params)
        if (fp.name == q.name) q.def = fp.defaultValue;
    n.effect = found;
    t.push_back(std::move(n));
  }
  for (size_t i = 0; i < t.size(); ++i) index_[t[i].id] = i;
}

const NodeType* NodeLibrary::find(const std::string& id) const {
  auto it = index_.find(id);
  return it == index_.end() ? nullptr : &types_[it->second];
}

json NodeLibrary::describe() const {
  const auto ports = [](const std::vector<Port>& list) {
    json out = json::array();
    for (const Port& p : list) out.push_back({{"name", p.name}, {"type", typeName(p.type)}, {"def", p.def}, {"min", p.min}, {"max", p.max}, {"text", p.text}});
    return out;
  };
  json out = json::array();
  for (const NodeType& t : types_) {
    json attrs = json::array();
    for (const Attr& a : t.attrs) attrs.push_back({{"name", a.name}, {"label", a.label}, {"type", a.type}, {"def", a.def}, {"options", a.options}});
    out.push_back({{"id", t.id}, {"name", t.name}, {"category", t.category}, {"description", t.description}, {"inputs", ports(t.inputs)}, {"outputs", ports(t.outputs)}, {"attrs", attrs}});
  }
  return out;
}

}  // namespace vj::patch
