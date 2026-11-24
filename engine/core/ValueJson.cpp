#include "core/ValueJson.h"

using nlohmann::json;

namespace vj::control {

json toJson(const Value& v) {
  return std::visit([](const auto& x) { return json(x); }, v);
}

std::optional<Value> fromJson(const json& j) {
  if (j.is_boolean()) return Value{j.get<bool>()};
  if (j.is_number_integer()) return Value{j.get<int64_t>()};
  if (j.is_number()) return Value{j.get<double>()};
  if (j.is_string()) return Value{j.get<std::string>()};
  return std::nullopt;
}

json describeJson(const ParamDesc& d, const std::optional<Value>& value) {
  static const char* types[] = {"bool", "int", "float", "string", "trigger", "enum"};
  json j = {{"path", d.path},   {"type", types[static_cast<int>(d.type)]}, {"readOnly", d.readOnly},
            {"label", d.label}, {"group", d.group},                        {"unit", d.unit},
            {"min", d.min},     {"max", d.max},                            {"milestone", d.milestone}};
  if (!d.options.empty()) j["options"] = d.options;
  if (value) j["value"] = toJson(*value);
  return j;
}

}  // namespace vj::control
