#include "control/ApiDispatcher.h"

using nlohmann::json;

namespace vj::control {

json ApiDispatcher::handle(const json& req, Subscriptions& subs) {
  json res = {{"type", "response"}};
  if (req.is_object() && req.contains("id")) res["id"] = req["id"];
  const auto fail = [&](const std::string& message) {
    res["ok"] = false;
    res["error"] = message;
    return res;
  };
  const auto finish = [&](ParamError e) {
    if (e != ParamError::None) return fail(describe(e));
    res["ok"] = true;
    return res;
  };
  if (!req.is_object() || !req.contains("action") || !req["action"].is_string()) return fail("falta action");
  const std::string action = req["action"];

  if (action == "list") {
    json all = json::array();
    for (const auto& d : params_.list()) all.push_back(describeJson(d, params_.get(d.path)));
    res["ok"] = true;
    res["parameters"] = std::move(all);
    return res;
  }

  if (action == "monitor") {
    subs.monitor = req.value("value", false);
    res["ok"] = true;
    return res;
  }
  if (action == "command") {
    if (!commands_) return fail("comandos no disponibles");
    if (!req.contains("name") || !req["name"].is_string()) return fail("falta name");
    json reply = commands_->call(req["name"], req.value("args", json::object()));
    if (!reply.value("ok", false)) return fail(reply.value("error", "error"));
    res["ok"] = true;
    res["result"] = std::move(reply["result"]);
    return res;
  }

  if (!req.contains("parameter") || !req["parameter"].is_string()) return fail("falta parameter");
  const std::string path = req["parameter"];
  res["parameter"] = path;

  if (action == "subscribe" || action == "unsubscribe") {
    if (path != "*" && !params_.describe(path)) return fail(describe(ParamError::UnknownPath));
    if (action == "subscribe") subs.paths.insert(path);
    else subs.paths.erase(path);
    res["ok"] = true;
    if (action == "subscribe" && path != "*")
      if (auto v = params_.get(path)) res["value"] = toJson(*v);
    return res;
  }
  if (action == "get") {
    if (!params_.describe(path)) return fail(describe(ParamError::UnknownPath));
    auto v = params_.get(path);
    if (!v) return fail(describe(ParamError::IsTrigger));
    res["ok"] = true;
    res["value"] = toJson(*v);
    return res;
  }
  if (action == "set") {
    if (!req.contains("value")) return fail("falta value");
    auto v = fromJson(req["value"]);
    if (!v) return fail(describe(ParamError::TypeMismatch));
    const auto before = params_.get(path);
    const auto e = params_.set(path, *v);
    if (e == ParamError::None)
      if (auto now = params_.get(path)) {
        res["value"] = toJson(*now);
        if (history_ && before && req.value("history", false)) history_(path, *before, *now);
      }
    return finish(e);
  }
  if (action == "reset") {
    const auto e = params_.reset(path);
    if (e == ParamError::None)
      if (auto now = params_.get(path)) res["value"] = toJson(*now);
    return finish(e);
  }
  if (action == "trigger") return finish(params_.trigger(path));
  return fail("acción desconocida: " + action);
}

std::string ApiDispatcher::handleText(const std::string& text, Subscriptions& subs) {
  json req = json::parse(text, nullptr, false);
  if (req.is_discarded()) return json{{"type", "response"}, {"ok", false}, {"error", "JSON no válido"}}.dump();
  return handle(req, subs).dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace vj::control
