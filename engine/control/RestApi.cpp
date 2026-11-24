#include "control/RestApi.h"

#include <regex>

#include "core/ValueJson.h"

using nlohmann::json;

namespace vj::control {
namespace {

HttpResponse ok(const json& j) { return {200, "application/json", j.dump(-1, ' ', false, json::error_handler_t::replace)}; }
HttpResponse fail(int status, const std::string& message) { return {status, "application/json", json{{"error", message}}.dump()}; }

std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::string decodeBase64(const std::string& in) {
  std::string out;
  int value = 0, bits = -8;
  for (unsigned char c : in) {
    int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
    if (d < 0) break;
    value = (value << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

HttpResponse runCommand(CommandBus* commands, const std::string& name, const json& args) {
  if (!commands) return fail(503, "comandos no disponibles");
  json r = commands->call(name, args);
  if (!r.value("ok", false)) return fail(400, r.value("error", std::string("error")));
  return ok(r.value("result", json::object()));
}

}  // namespace

json restRoutes() {
  return json::array({
      {{"method", "GET"}, {"path", "/api/v1/routes"}, {"summary", "Esta lista"}},
      {{"method", "GET"}, {"path", "/api/v1/product"}, {"summary", "Nombre y versión"}},
      {{"method", "GET"}, {"path", "/api/v1/composition"}, {"summary", "Estructura: capas, columnas, grupos, decks"}},
      {{"method", "GET"}, {"path", "/api/v1/parameters"}, {"summary", "Todos los parámetros con su valor"}},
      {{"method", "GET"}, {"path", "/api/v1/parameter/{ruta}"}, {"summary", "Un parámetro"}},
      {{"method", "PUT"}, {"path", "/api/v1/parameter/{ruta}"}, {"summary", "Cambia un valor: {\"value\": ...}"}},
      {{"method", "DELETE"}, {"path", "/api/v1/parameter/{ruta}"}, {"summary", "Vuelve al valor por defecto"}},
      {{"method", "POST"}, {"path", "/api/v1/trigger/{ruta}"}, {"summary", "Dispara un parámetro de evento"}},
      {{"method", "POST"}, {"path", "/api/v1/composition/layers/{l}/clips/{c}/connect"}, {"summary", "Dispara un clip"}},
      {{"method", "POST"}, {"path", "/api/v1/composition/columns/{c}/connect"}, {"summary", "Dispara una columna"}},
      {{"method", "GET"}, {"path", "/api/v1/composition/layers/{l}/clips/{c}/thumbnail"}, {"summary", "Miniatura del clip en JPEG"}},
      {{"method", "GET"}, {"path", "/api/v1/effects"}, {"summary", "Efectos y fuentes disponibles"}},
      {{"method", "GET"}, {"path", "/api/v1/effects/{entidad}"}, {"summary", "Cadena de efectos de una entidad"}},
      {{"method", "POST"}, {"path", "/api/v1/effects/{entidad}"}, {"summary", "Añade un efecto: {\"type\": ...}"}},
      {{"method", "DELETE"}, {"path", "/api/v1/effects/{entidad}/{id}"}, {"summary", "Quita un efecto"}},
      {{"method", "POST"}, {"path", "/api/v1/command/{nombre}"}, {"summary", "Cualquier comando interno, con sus argumentos"}},
  });
}

HttpResponse handleRest(const std::string& method, const std::string& rawPath, const std::string& body, ParameterRegistry& params, CommandBus* commands) {
  std::string path = rawPath.substr(0, rawPath.find('?'));
  const std::string base = "/api/v1";
  if (path.rfind(base, 0) != 0) return fail(404, "no encontrado");
  path = urlDecode(path.substr(base.size()));
  json input = json::object();
  if (!body.empty()) {
    input = json::parse(body, nullptr, false);
    if (input.is_discarded()) return fail(400, "JSON no válido");
  }

  if (method == "GET" && path == "/routes") return ok(restRoutes());
  if (method == "GET" && path == "/product") return ok({{"name", "VJ Studio"}, {"version", params.getString("/engine/version")}});
  if (method == "GET" && path == "/composition")
    return ok({{"layers", params.getInt("/composition/structure/layers")}, {"columns", params.getInt("/composition/structure/columns")},
               {"groups", params.getInt("/composition/structure/groups")}, {"deck", params.getInt("/composition/deck")},
               {"decks", json::parse(params.getString("/project/decks", "[]"), nullptr, false)},
               {"bpm", params.getFloat("/composition/tempo/bpm")}});
  if (method == "GET" && path == "/parameters") {
    json all = json::array();
    for (const auto& d : params.list()) all.push_back(describeJson(d, params.get(d.path)));
    return ok(all);
  }
  if (path.rfind("/parameter/", 0) == 0) {
    const std::string p = path.substr(10);
    const auto d = params.describe(p);
    if (!d) return fail(404, "parámetro desconocido");
    if (method == "GET") return ok(describeJson(*d, params.get(p)));
    if (method == "DELETE") {
      const auto e = params.reset(p);
      return e == ParamError::None ? ok(json{{"value", toJson(*params.get(p))}}) : fail(409, describe(e));
    }
    if (method == "PUT") {
      if (!input.contains("value")) return fail(400, "falta value");
      const auto v = fromJson(input["value"]);
      if (!v) return fail(400, "valor no válido");
      const auto e = params.set(p, *v);
      if (e != ParamError::None) return fail(409, describe(e));
      return ok(json{{"value", toJson(*params.get(p))}});
    }
    return fail(405, "método no admitido");
  }
  if (path.rfind("/trigger/", 0) == 0 && method == "POST") {
    const auto e = params.trigger(path.substr(8));
    return e == ParamError::None ? ok(json::object()) : fail(e == ParamError::UnknownPath ? 404 : 409, describe(e));
  }
  static const std::regex connect(R"(^/composition/(layers/\d+/clips/\d+|columns/\d+)/connect$)");
  if (method == "POST" && std::regex_match(path, connect)) {
    const auto e = params.trigger(path);
    return e == ParamError::None ? ok(json::object()) : fail(404, describe(e));
  }
  static const std::regex thumb(R"(^(/composition/layers/\d+/clips/\d+)/thumbnail$)");
  std::smatch m;
  if (method == "GET" && std::regex_match(path, m, thumb)) {
    const std::string url = params.getString(m[1].str() + "/thumbnail");
    const auto comma = url.find(',');
    if (comma == std::string::npos) return fail(404, "sin miniatura");
    return {200, "image/jpeg", decodeBase64(url.substr(comma + 1))};
  }
  if (path == "/effects" && method == "GET") return runCommand(commands, "effects.catalog", json::object());
  if (path.rfind("/effects/", 0) == 0) {
    static const std::regex withId(R"(^/effects(/.*)/(\d+)$)");
    if (method == "DELETE" && std::regex_match(path, m, withId)) return runCommand(commands, "effect.remove", {{"entity", m[1].str()}, {"id", std::stoi(m[2].str())}});
    const std::string entity = path.substr(8);
    if (method == "GET") return runCommand(commands, "effects.list", {{"entity", entity}});
    if (method == "POST") return runCommand(commands, "effect.add", {{"entity", entity}, {"type", input.value("type", std::string{})}});
  }
  if (path.rfind("/command/", 0) == 0 && method == "POST") return runCommand(commands, path.substr(9), input);
  return fail(404, "no encontrado");
}

}  // namespace vj::control
