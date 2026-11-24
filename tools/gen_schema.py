#!/usr/bin/env python3
"""Genera C++ y TypeScript desde schemas/. Con --check falla si difieren."""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
schema = json.loads((ROOT / "schemas/parameters.json").read_text(encoding="utf-8"))
modes = json.loads((ROOT / "schemas/blendmodes.json").read_text(encoding="utf-8"))
effects = json.loads((ROOT / "schemas/effects.json").read_text(encoding="utf-8"))
params = schema["parameters"]
templates = schema["templates"]
HEADER = "Generado por tools/gen_schema.py desde schemas/. No editar."


def cpp_str(s):
    return json.dumps(s, ensure_ascii=False)


def cpp_default(p):
    t, d = p["type"], p.get("default")
    if t == "trigger":
        return "Value{false}"
    if t == "bool":
        return "Value{%s}" % ("true" if d else "false")
    if t == "int":
        return "Value{int64_t{%d}}" % int(d)
    if t == "float":
        return "Value{double{%r}}" % float(d)
    return "Value{std::string{%s}}" % cpp_str(d)


def cpp_row(p):
    types = {"bool": "Bool", "int": "Int", "float": "Float", "string": "String", "trigger": "Trigger", "enum": "Enum"}
    opts = ", ".join(cpp_str(o) for o in p.get("options", []))
    return "    {%s, ParamType::%s, %s, %r, %r, %s, %s, %s, %s, %s, {%s}}," % (
        cpp_str(p["path"]), types[p["type"]], cpp_default(p),
        float(p.get("min", 0)), float(p.get("max", 0)),
        "true" if p.get("readOnly") else "false",
        cpp_str(p.get("label", "")), cpp_str(p.get("group", "")),
        cpp_str(p.get("unit", "")), cpp_str(p["milestone"]), opts)


def cpp_table(name, rows):
    return ["inline const std::vector<ParamDesc>& %s() {" % name,
            "  static const std::vector<ParamDesc> table = {", *[cpp_row(p) for p in rows], "  };",
            "  return table;", "}", ""]


def gen_cpp():
    out = ["// " + HEADER, "#pragma once", '#include "core/Parameter.h"', "#include <vector>", "",
           "namespace vj::schema {", ""]
    out += cpp_table("parameters", params)
    for key, t in templates.items():
        out += ["// Prefijo: " + t["prefix"]]
        out += cpp_table(key + "Parameters", t["parameters"])
    out += ["}  // namespace vj::schema", ""]
    return "\n".join(out)


def gen_modes():
    def raw(s):
        return 'R"(' + s + ')"'
    out = ["// " + HEADER, "#pragma once", "#include <vector>", "", "namespace vj::schema {", "",
           "struct BlendMode {", "  const char* name;", "  const char* blend;  // función de mezcla, o nullptr",
           "  const char* raw;    // cuerpo completo, o nullptr", "};", "",
           "struct TransitionMode {", "  const char* name;", "  const char* glsl;", "};", "",
           "inline const std::vector<BlendMode>& blendModes() {", "  static const std::vector<BlendMode> table = {"]
    for b in modes["blends"]:
        out.append("    {%s, %s, %s}," % (cpp_str(b["name"]), raw(b["blend"]) if "blend" in b else "nullptr",
                                          raw(b["raw"]) if "raw" in b else "nullptr"))
    out += ["  };", "  return table;", "}", "",
            "inline const std::vector<TransitionMode>& transitionModes() {", "  static const std::vector<TransitionMode> table = {"]
    for t in modes["transitions"]:
        out.append("    {%s, %s}," % (cpp_str(t["name"]), raw(t["glsl"])))
    out += ["  };", "  return table;", "}", "", "}  // namespace vj::schema", ""]
    return "\n".join(out)


def gen_effects():
    def raw(s):
        return 'R"GLSL(' + s + ')GLSL"'
    out = ["// " + HEADER, "#pragma once", "#include <string>", "#include <vector>", "", "namespace vj::schema {", "",
           "struct EffectParam {", "  std::string name, label, type;  // type: float, bool o string", "  double min, max, defaultValue;",
           "  std::string defaultText;", "};", "",
           "struct EffectType {", "  std::string id, name, category;", "  bool source;", "  std::vector<EffectParam> params;",
           "  std::string glsl;", "  int memoryFrames = 0;  // fotogramas que guarda cada instancia", "  bool memoryOutput = false;  // guarda su salida (realimentación) o su entrada (retardo)",
           "  int ageA = -1, ageB = -1;  // parámetro con la antigüedad de mem() y mem2(); -1: el último", "};", "", "inline const std::vector<EffectType>& effectTypes() {",
           "  static const std::vector<EffectType> table = {"]
    for kind, source in (("effects", "false"), ("sources", "true"), ("audio", "false")):
        for e in effects[kind]:
            ps = []
            for p in e["params"]:
                text = p.get("default") if p.get("type") == "string" else ""
                num = 0 if p.get("type") == "string" else p["default"]
                ps.append("{%s, %s, %s, %r, %r, %r, %s}" % (cpp_str(p["name"]), cpp_str(p["label"]), cpp_str(p.get("type", "float")),
                          float(p.get("min", 0)), float(p.get("max", 0)), float(num), cpp_str(text)))
            m = e.get("memory", {})
            out.append("    {%s, %s, %s, %s, {%s}, %s, %d, %s, %d, %d}," % (cpp_str(e["id"]), cpp_str(e["name"]), cpp_str(e["category"]), source, ", ".join(ps), raw("//AUDIO" if kind == "audio" else e["glsl"]),
                       m.get("frames", 0), "true" if m.get("output") else "false", m.get("ageA", -1), m.get("ageB", -1)))
    out += ["  };", "  return table;", "}", "", "}  // namespace vj::schema", ""]
    return "\n".join(out)


def gen_ts():
    table = json.dumps(params, ensure_ascii=False, indent=2)
    return "\n".join([
        "// " + HEADER,
        "export type ParamType = 'bool' | 'int' | 'float' | 'string' | 'trigger' | 'enum';",
        "export interface ParamDesc {",
        "  path: string; type: ParamType; default?: boolean | number | string;",
        "  min?: number; max?: number; readOnly?: boolean; label?: string; group?: string; unit?: string;",
        "  options?: string[]; milestone: string;",
        "}",
        "export const SCHEMA_VERSION = %d;" % schema["version"],
        "export const PARAMETERS: readonly ParamDesc[] = " + table + ";",
        "export const TEMPLATE_KEYS = %s as const;" % json.dumps(list(templates)),
        ""])


targets = {
    ROOT / "engine/core/generated/Parameters.gen.h": gen_cpp(),
    ROOT / "engine/core/generated/BlendModes.gen.h": gen_modes(),
    ROOT / "engine/core/generated/Effects.gen.h": gen_effects(),
    ROOT / "ui/packages/api-client/src/generated.ts": gen_ts(),
}
stale = []
for path, text in targets.items():
    if "--check" in sys.argv:
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            stale.append(str(path.relative_to(ROOT)))
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
if stale:
    sys.exit("Desalineado con schemas/: " + ", ".join(stale) + " (ejecuta tools/gen_schema.py)")
