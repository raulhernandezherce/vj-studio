#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "project/Document.h"
#include "project/ProjectModule.h"
#include "project/Restructure.h"

using namespace vj;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
const std::string kRed = "vj://solid/FF0000", kBlue = "vj://solid/0000FF";
}

class ProjectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_project_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    fs::create_directories(dir);
    params.defineAll(schema::parameters());
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{3, 4, 2}));
    modules.add(std::make_unique<project::ProjectModule>((dir / "docs").string()));
    ASSERT_EQ(modules.startAll(ctx), "");
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
    proj = modules.get<project::ProjectModule>("project");
  }
  void TearDown() override {
    modules.stopAll();
    fs::remove_all(dir);
  }
  std::string C(int l, int c) { return "/composition/layers/" + std::to_string(l) + "/clips/" + std::to_string(c); }
  void settle() {
    for (int i = 0; i < 4; ++i) {
      modules.tickAll(0);
      comp->waitForLoads();
    }
    modules.tickAll(0);
  }
  void load(int l, int c, const std::string& path) {
    params.set(C(l, c) + "/path", path);
    settle();
  }
  json cmd(const std::string& name, const json& args = json::object()) {
    auto r = commands.run(name, args);
    settle();
    return r;
  }
  std::string path(int l, int c) { return params.getString(C(l, c) + "/path"); }
  // Un cambio hecho a mano en la interfaz.
  void handSet(const std::string& p, const Value& v) {
    const auto before = *params.get(p);
    params.set(p, v);
    proj->recordSet(p, before, *params.get(p));
  }

  static inline int counter = 0;
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  MonitorBus monitors;
  EngineContext ctx{params, events, modules, &commands, &monitors};
  compositor::Composition* comp = nullptr;
  project::ProjectModule* proj = nullptr;
};

TEST_F(ProjectTest, CreatesUserFolders) {
  for (const char* d : {"Compositions", "Recorded", "Presets/Interface", "Preferences"}) EXPECT_TRUE(fs::is_directory(dir / "docs" / d)) << d;
  EXPECT_EQ(params.getString("/project/name"), "Sin título");
}

TEST_F(ProjectTest, SaveOpenRoundTrip) {
  load(1, 1, kRed);
  params.set(C(1, 1) + "/video/opacity", 0.25);
  params.set(C(1, 1) + "/color", std::string("Verde"));
  params.set("/composition/layers/2/blend", std::string("Multiply"));
  params.set("/composition/tempo/bpm", 90.0);
  params.set("/composition/notes", std::string("hola"));
  cmd("deck.add", {{"name", "Segundo"}});
  cmd("deck.select", {{"index", 1}});
  load(2, 2, kBlue);
  const auto out = cmd("project.save", {{"path", (dir / "show").string()}});
  EXPECT_EQ(out["path"], (dir / "show.vjcomp").string());
  EXPECT_FALSE(params.getBool("/project/dirty"));
  EXPECT_EQ(params.getString("/project/name"), "show");

  cmd("project.new");
  EXPECT_EQ(path(1, 1), "");
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/tempo/bpm"), 120);

  cmd("project.open", {{"path", out["path"]}});
  EXPECT_EQ(path(2, 2), kBlue);
  EXPECT_EQ(params.getInt("/composition/deck"), 2);
  EXPECT_EQ(params.getString("/composition/layers/2/blend"), "Multiply");
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/tempo/bpm"), 90);
  EXPECT_EQ(params.getString("/composition/notes"), "hola");
  cmd("deck.select", {{"index", 0}});
  EXPECT_EQ(path(1, 1), kRed);
  EXPECT_DOUBLE_EQ(params.getFloat(C(1, 1) + "/video/opacity"), 0.25);
  EXPECT_EQ(params.getString(C(1, 1) + "/color"), "Verde");
  EXPECT_EQ(params.getString(C(1, 1) + "/status"), "ready");
}

TEST_F(ProjectTest, OpenFailureLeavesProjectUntouched) {
  load(1, 1, kRed);
  std::ofstream(dir / "mal.vjcomp") << "{no";
  EXPECT_THROW(commands.run("project.open", {{"path", (dir / "mal.vjcomp").string()}}), std::runtime_error);
  EXPECT_THROW(commands.run("project.open", {{"path", (dir / "no.vjcomp").string()}}), std::runtime_error);
  EXPECT_EQ(path(1, 1), kRed);
}

TEST(Document, MigratesOldAndRejectsFuture) {
  json old = {{"format", project::kFormatName}, {"version", 0}, {"cells", {{"0/0", {{"path", "a"}}}}}};
  json doc = project::migrate(old);
  EXPECT_EQ(doc["version"], project::kFormatVersion);
  EXPECT_EQ(doc["decks"][0]["cells"]["0/0"]["path"], "a");
  EXPECT_THROW(project::migrate({{"format", project::kFormatName}, {"version", 999}}), std::runtime_error);
  EXPECT_THROW(project::migrate({{"format", "otra"}}), std::runtime_error);
}

TEST(Document, SiblingPathKeepsRelativePosition) {
  EXPECT_EQ(project::siblingPath("/old/show/a/x.mp4", "/new/lib/a/x.mp4", "/old/show/b/y.mp4"), "/new/lib/b/y.mp4");
  EXPECT_EQ(project::siblingPath("/old/x.mp4", "/new/x.mp4", "/otro/y.mp4"), "");
}

TEST_F(ProjectTest, DecksSwapContent) {
  load(1, 1, kRed);
  cmd("deck.add");
  cmd("deck.select", {{"index", 1}});
  EXPECT_EQ(path(1, 1), "");
  load(1, 1, kBlue);
  cmd("deck.select", {{"index", 0}});
  EXPECT_EQ(path(1, 1), kRed);
  cmd("deck.select", {{"index", 1}});
  EXPECT_EQ(path(1, 1), kBlue);
}

TEST_F(ProjectTest, PlayingClipSurvivesDeckChangeAndRebinds) {
  load(1, 1, kRed);
  params.trigger(C(1, 1) + "/connect");
  settle();
  EXPECT_TRUE(params.getBool(C(1, 1) + "/connected"));
  cmd("deck.add");
  cmd("deck.select", {{"index", 1}});
  EXPECT_FALSE(params.getBool(C(1, 1) + "/connected")) << "la celda del deck nuevo no suena";
  EXPECT_TRUE(comp->plan().layers[0].current.has_value()) << "la salida sigue";
  cmd("deck.select", {{"index", 0}});
  EXPECT_TRUE(params.getBool(C(1, 1) + "/connected"));
}

TEST_F(ProjectTest, PersistentClipAppearsInAllDecksAndCoveredIsRecoverable) {
  cmd("deck.add");
  cmd("deck.select", {{"index", 1}});
  load(1, 1, kBlue);  // este quedará tapado
  cmd("deck.select", {{"index", 0}});
  load(1, 1, kRed);
  params.set(C(1, 1) + "/persistent", true);
  cmd("deck.select", {{"index", 1}});
  EXPECT_EQ(path(1, 1), kRed);
  EXPECT_EQ(params.getString(C(1, 1) + "/covered"), kBlue);
  cmd("clip.releaseCovered", {{"layer", 1}, {"column", 1}});
  EXPECT_EQ(path(1, 2), kBlue);
  EXPECT_EQ(params.getString(C(1, 1) + "/covered"), "");
}

TEST_F(ProjectTest, DeckRenameDuplicateRemoveRules) {
  cmd("deck.rename", {{"index", 0}, {"name", "Fondos"}});
  load(1, 1, kRed);
  cmd("deck.duplicate", {{"index", 0}});
  auto decks = json::parse(params.getString("/project/decks"));
  EXPECT_EQ(decks.size(), 2u);
  EXPECT_EQ(decks[0]["name"], "Fondos");
  EXPECT_EQ(decks[1]["clips"], 1);
  cmd("deck.remove", {{"index", 1}});
  EXPECT_THROW(commands.run("deck.remove", {{"index", 0}}), std::runtime_error);
}

TEST_F(ProjectTest, ClipCopyPasteMoveSwapClear) {
  load(1, 1, kRed);
  load(1, 2, kBlue);
  params.set(C(1, 1) + "/name", std::string("Rojo"));
  cmd("clip.copy", {{"layer", 1}, {"column", 1}});
  cmd("clip.paste", {{"layer", 2}, {"column", 3}});
  EXPECT_EQ(path(2, 3), kRed);
  EXPECT_EQ(params.getString(C(2, 3) + "/name"), "Rojo");
  cmd("clip.move", {{"layer", 1}, {"column", 1}, {"toLayer", 1}, {"toColumn", 2}});
  EXPECT_EQ(path(1, 1), kBlue);
  EXPECT_EQ(path(1, 2), kRed);
  cmd("clip.cut", {{"layer", 1}, {"column", 1}});
  EXPECT_EQ(path(1, 1), "");
  cmd("clip.paste", {{"layer", 3}, {"column", 1}});
  EXPECT_EQ(path(3, 1), kBlue);
}

TEST_F(ProjectTest, HundredUndoRedoWithoutDivergence) {
  const json initial = proj->capture();
  for (int i = 0; i < 100; ++i) {
    const std::string p = "/composition/layers/" + std::to_string(i % 3 + 1) + (i % 2 ? "/opacity" : "/master");
    handSet(p, (i % 10) / 10.0);
    modules.tickAll(1.0);  // separados: no se funden en uno
    if (i % 25 == 0) cmd("deck.add");
  }
  const json final = proj->capture();
  EXPECT_NE(initial, final);
  for (int i = 0; i < 200 && params.getBool("/project/canundo"); ++i) cmd("history.undo");
  EXPECT_EQ(proj->capture(), initial);
  for (int i = 0; i < 200 && params.getBool("/project/canredo"); ++i) cmd("history.redo");
  EXPECT_EQ(proj->capture(), final);
}

TEST_F(ProjectTest, DragCoalescesIntoOneStep) {
  handSet("/composition/master", 0.9);
  handSet("/composition/master", 0.8);
  handSet("/composition/master", 0.7);
  cmd("history.undo");
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 1);
  EXPECT_FALSE(params.getBool("/project/canundo"));
}

TEST_F(ProjectTest, UndoReturnsToTheDeckOfTheChange) {
  load(1, 1, kRed);
  handSet(C(1, 1) + "/video/opacity", 0.3);
  modules.tickAll(1.0);
  cmd("deck.add");
  cmd("deck.select", {{"index", 1}});
  cmd("history.undo");  // deshace el deck añadido
  cmd("history.undo");
  EXPECT_EQ(params.getInt("/composition/deck"), 1);
  EXPECT_DOUBLE_EQ(params.getFloat(C(1, 1) + "/video/opacity"), 1);
}

TEST_F(ProjectTest, TriggersAndApiChangesAreNotUndoable) {
  load(1, 1, kRed);
  params.trigger(C(1, 1) + "/connect");
  params.set("/composition/master", 0.5);
  settle();
  EXPECT_FALSE(params.getBool("/project/canundo"));
}

TEST_F(ProjectTest, RelinkTwentyMovedFilesInOneOperation) {
  fs::create_directories(dir / "old" / "sub");
  fs::create_directories(dir / "new" / "sub");
  // Rutas ausentes: al abrir marcan error, pero siguen en el documento.
  std::vector<std::string> names;
  for (int i = 0; i < 20; ++i) {
    const std::string rel = std::string(i % 2 ? "sub/" : "") + "clip" + std::to_string(i) + ".png";
    std::ofstream(dir / "new" / rel) << "x";
    names.push_back(rel);
    if (i % 4 == 0 && i) cmd("deck.add"), cmd("deck.select", {{"index", i / 4}});
    params.set(C(i % 3 + 1, i % 4 + 1) + "/path", (dir / "old" / rel).string());
  }
  settle();
  auto list = cmd("media.list");
  EXPECT_EQ(list.size(), 20u);
  for (const auto& m : list) EXPECT_FALSE(m["exists"]);
  auto r = cmd("media.relink", {{"from", (dir / "old" / names[0]).string()}, {"to", (dir / "new" / names[0]).string()}});
  EXPECT_EQ(r["fixed"], 20);
  for (const auto& m : cmd("media.list")) EXPECT_TRUE(m["exists"]) << m;
}

TEST_F(ProjectTest, CollectCopiesAndNeverDeletes) {
  const auto src = dir / "a.png";
  std::ofstream(src) << "x";
  params.set(C(1, 1) + "/path", src.string());
  settle();
  auto r = cmd("media.collect", {{"dir", (dir / "pack").string()}});
  EXPECT_EQ(r["copied"], 1);
  EXPECT_TRUE(fs::exists(src));
  EXPECT_TRUE(fs::exists(dir / "pack" / "Media" / "Deck 1" / "a.png"));
  auto doc = project::readDocument(r["path"]);
  EXPECT_EQ(doc["decks"][0]["cells"]["0/0"]["path"], (dir / "pack" / "Media" / "Deck 1" / "a.png").string());
}

TEST_F(ProjectTest, DashboardDrivesLinkedParameters) {
  const std::string knob = "/composition/dashboard/k1";
  cmd("dashboard.assign", {{"knob", knob}, {"param", "/composition/master"}, {"invert", true}});
  cmd("dashboard.assign", {{"knob", knob}, {"param", "/composition/bypass"}});
  params.set(knob, 0.75);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.25);
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  cmd("dashboard.unassign", {{"knob", knob}, {"param", "bypass"}});
  params.set(knob, 0.2);
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.8);
}

TEST_F(ProjectTest, ClipDashboardMapsEnumRelatively) {
  load(1, 1, kRed);
  const std::string knob = C(1, 1) + "/dashboard/k2";
  cmd("dashboard.assign", {{"knob", knob}, {"param", C(1, 1) + "/resize"}});
  params.set(knob, 0.99);
  EXPECT_EQ(params.getString(C(1, 1) + "/resize"), "Original");
}

TEST_F(ProjectTest, ThumbnailGeneratedUpdatedAndReverted) {
  load(1, 1, "vj://pattern");
  const std::string original = params.getString(C(1, 1) + "/thumbnail");
  EXPECT_EQ(original.rfind("data:image/jpeg;base64,", 0), 0u);
  params.trigger(C(1, 1) + "/connect");
  settle();
  modules.tickAll(1.0);
  cmd("clip.thumbnail", {{"layer", 1}, {"column", 1}, {"mode", "current"}});
  EXPECT_TRUE(params.getBool(C(1, 1) + "/thumbnailcustom"));
  EXPECT_NE(params.getString(C(1, 1) + "/thumbnail"), original);
  cmd("clip.thumbnail", {{"layer", 1}, {"column", 1}, {"mode", "revert"}});
  EXPECT_EQ(params.getString(C(1, 1) + "/thumbnail"), original);
}

TEST_F(ProjectTest, PreviewDoesNotTouchOutput) {
  load(1, 1, kRed);
  load(1, 2, kBlue);
  params.trigger(C(1, 1) + "/connect");
  settle();
  cmd("preview.clip", {{"layer", 1}, {"column", 2}});
  modules.tickAll(0.016);
  EXPECT_EQ(params.getInt("/preview/column"), 2);
  EXPECT_EQ(params.getInt("/composition/layers/1/activeclip"), 1);
  auto plan = comp->previewPlan(160, 90);
  ASSERT_TRUE(plan.layers[0].current);
  EXPECT_EQ(plan.layers[0].current->frame->pixels[2], 0xFF) << "preview azul";
  EXPECT_EQ(comp->plan().layers[0].current->frame->pixels[0], 0xFF) << "salida roja";
  cmd("preview.stop");
  EXPECT_FALSE(comp->previewActive());
}

TEST_F(ProjectTest, PreviewFileWithoutCell) {
  cmd("preview.file", {{"path", kRed}});
  for (int i = 0; i < 100 && !comp->previewActive(); ++i) {
    modules.tickAll(0.01);
    usleep(1000);
  }
  EXPECT_TRUE(comp->previewActive());
  EXPECT_EQ(path(1, 1), "");
}

TEST_F(ProjectTest, PreferencesPersistBetweenSessions) {
  params.set("/preferences/defaultplaymode", std::string("Ping Pong"));
  modules.tickAll(0);
  ParameterRegistry again;
  again.defineAll(schema::parameters());
  EventBus events2;
  ModuleRegistry modules2;
  CommandBus commands2;
  EngineContext ctx2{again, events2, modules2, &commands2, nullptr};
  modules2.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{1, 1, 1}));
  modules2.add(std::make_unique<project::ProjectModule>((dir / "docs").string()));
  ASSERT_EQ(modules2.startAll(ctx2), "");
  EXPECT_EQ(again.getString("/preferences/defaultplaymode"), "Ping Pong");
  modules2.stopAll();
}

TEST_F(ProjectTest, DefaultsApplyToImportedClips) {
  params.set("/preferences/defaultplaymode", std::string("Ping Pong"));
  params.set("/preferences/defaultblend", std::string("Screen"));
  load(1, 1, kRed);
  EXPECT_EQ(params.getString(C(1, 1) + "/transport/playmode"), "Ping Pong");
  EXPECT_EQ(params.getString(C(1, 1) + "/video/blend"), "Screen");
}

TEST_F(ProjectTest, EffectChainSurvivesSaveOpenAndDecks) {
  load(1, 1, kRed);
  const std::string clip = C(1, 1);
  const int fx = cmd("effect.add", {{"entity", clip}, {"type", "hue"}})["id"];
  params.set(clip + "/effects/e" + std::to_string(fx) + "/amount", 0.4);
  const int lay = cmd("effect.add", {{"entity", "/composition/layers/2"}, {"type", "blur"}})["id"];
  cmd("effect.add", {{"entity", "/composition"}, {"type", "invert"}});
  const auto saved = cmd("project.save", {{"path", (dir / "fx").string()}})["path"].get<std::string>();
  cmd("project.new");
  EXPECT_TRUE(comp->effects().empty(clip));
  EXPECT_TRUE(comp->effects().empty("/composition"));
  EXPECT_FALSE(params.describe(clip + "/effects/e" + std::to_string(fx) + "/amount"));
  cmd("project.open", {{"path", saved}});
  EXPECT_DOUBLE_EQ(params.getFloat(clip + "/effects/e" + std::to_string(fx) + "/amount"), 0.4);
  EXPECT_TRUE(params.describe("/composition/layers/2/effects/e" + std::to_string(lay) + "/radius"));
  EXPECT_FALSE(comp->effects().empty("/composition/layers/2"));
  EXPECT_FALSE(comp->effects().empty("/composition"));

  cmd("deck.add");
  cmd("deck.select", {{"index", 1}});
  EXPECT_TRUE(comp->effects().empty(clip)) << "la celda del deck nuevo no tiene efectos";
  cmd("deck.select", {{"index", 0}});
  EXPECT_DOUBLE_EQ(params.getFloat(clip + "/effects/e" + std::to_string(fx) + "/amount"), 0.4);
}

TEST_F(ProjectTest, EffectEditsUndo) {
  const std::string e = "/composition/layers/1";
  const int id = cmd("effect.add", {{"entity", e}, {"type", "hue"}})["id"];
  const std::string amount = e + "/effects/e" + std::to_string(id) + "/amount";
  handSet(amount, 0.5);
  modules.tickAll(1.0);
  cmd("effect.remove", {{"entity", e}, {"id", id}});
  EXPECT_FALSE(params.describe(amount));
  cmd("history.undo");
  EXPECT_DOUBLE_EQ(params.getFloat(amount), 0.5);
  cmd("history.undo");
  EXPECT_DOUBLE_EQ(params.getFloat(amount), 0);
  cmd("history.undo");
  EXPECT_FALSE(params.describe(amount));
  cmd("history.redo");
  EXPECT_TRUE(params.describe(amount));
}

TEST_F(ProjectTest, PasteEffectsLeavesMediaAlone) {
  load(1, 1, kRed);
  load(1, 2, kBlue);
  load(1, 3, kBlue);
  load(1, 4, kBlue);
  const int id = cmd("effect.add", {{"entity", C(1, 1)}, {"type", "hue"}})["id"];
  params.set(C(1, 1) + "/effects/e" + std::to_string(id) + "/amount", 0.3);
  cmd("effects.copy", {{"entity", C(1, 1)}});
  std::vector<json> chains;
  for (int c = 2; c <= 4; ++c) {
    cmd("effects.paste", {{"entity", C(1, c)}});
    json chain = comp->effects().save(C(1, c));
    ASSERT_EQ(chain.size(), 1u);
    chain[0].erase("id");
    chains.push_back(chain);
    EXPECT_EQ(path(1, c), kBlue);
  }
  EXPECT_EQ(chains[0], chains[1]);
  EXPECT_EQ(chains[1], chains[2]);
  EXPECT_DOUBLE_EQ(chains[0][0]["values"]["amount"], 0.3);
}

TEST_F(ProjectTest, PresetCopiesValuesAndForgets) {
  const std::string e = "/composition/layers/1";
  const int a = cmd("effect.add", {{"entity", e}, {"type", "hue"}})["id"];
  const int b = cmd("effect.add", {{"entity", e}, {"type", "hue"}})["id"];
  const auto pa = e + "/effects/e" + std::to_string(a) + "/amount", pb = e + "/effects/e" + std::to_string(b) + "/amount";
  params.set(pa, 0.7);
  cmd("preset.save", {{"entity", e}, {"id", a}, {"name", "Cálido"}});
  EXPECT_EQ(cmd("preset.list", {{"kind", "hue"}}), json::array({"Cálido"}));
  cmd("preset.apply", {{"entity", e}, {"id", b}, {"kind", "hue"}, {"name", "Cálido"}});
  EXPECT_DOUBLE_EQ(params.getFloat(pb), 0.7);
  params.set(pa, 0.1);
  cmd("preset.save", {{"entity", e}, {"id", a}, {"name", "Cálido"}});
  EXPECT_DOUBLE_EQ(params.getFloat(pb), 0.7) << "un preset no mantiene efectos sincronizados";
}

TEST_F(ProjectTest, ChainPresetAndBadNames) {
  const std::string e = "/composition/layers/1";
  cmd("effect.add", {{"entity", e}, {"type", "hue"}});
  cmd("effect.add", {{"entity", e}, {"type", "blur"}});
  cmd("preset.save", {{"entity", e}, {"name", "Cadena"}});
  cmd("preset.apply", {{"entity", "/composition/layers/2"}, {"name", "Cadena"}});
  EXPECT_EQ(comp->effects().save("/composition/layers/2").size(), 2u);
  EXPECT_THROW(commands.run("preset.save", {{"entity", e}, {"name", "../x"}}), std::runtime_error);
}

TEST_F(ProjectTest, SourceInCell) {
  cmd("source.add", {{"layer", 1}, {"column", 1}, {"type", "plasma"}});
  EXPECT_EQ(path(1, 1), "vj://source/plasma");
  EXPECT_EQ(params.getString(C(1, 1) + "/status"), "ready");
  EXPECT_FALSE(comp->effects().empty(C(1, 1)));
  EXPECT_THROW(commands.run("source.add", {{"layer", 1}, {"column", 2}, {"type", "blur"}}), std::runtime_error);
  params.trigger(C(1, 1) + "/clear");
  settle();
  EXPECT_TRUE(comp->effects().empty(C(1, 1)));
}

TEST_F(ProjectTest, RejectsUnknownEffectAndEntity) {
  EXPECT_THROW(commands.run("effect.add", {{"entity", "/composition"}, {"type", "nada"}}), std::runtime_error);
  EXPECT_THROW(commands.run("effect.add", {{"entity", "/engine"}, {"type", "hue"}}), std::runtime_error);
}

TEST_F(ProjectTest, IsfFolderScanned) {
  std::ofstream(dir / "docs" / "Extra Effects" / "tinte.fs")
      << R"(/*{"INPUTS":[{"NAME":"inputImage","TYPE":"image"},{"NAME":"k","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1}]}*/
void main() { gl_FragColor = IMG_THIS_PIXEL(inputImage) * k; })";
  std::ofstream(dir / "docs" / "Extra Effects" / "mal.fs") << "sin cabecera";
  const auto errors = cmd("effects.rescan");
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0]["file"], "mal.fs");
  bool found = false;
  for (const auto& e : cmd("effects.catalog")) found = found || e["id"] == "isf:tinte";
  EXPECT_TRUE(found);
}

TEST(CommandBusTest, CrossThreadCallAndErrors) {
  CommandBus bus;
  bus.add("doble", [](const json& a) { return json{{"v", a.at("n").get<int>() * 2}}; });
  bus.add("falla", [](const json&) -> json { throw std::runtime_error("no"); });
  json ok, bad, unknown;
  std::thread t([&] {
    ok = bus.call("doble", {{"n", 4}});
    bad = bus.call("falla", json::object());
    unknown = bus.call("nada", json::object());
  });
  while (unknown.is_null()) bus.pump();
  t.join();
  EXPECT_EQ(ok["result"]["v"], 8);
  EXPECT_EQ(bad["error"], "no");
  EXPECT_FALSE(unknown["ok"]);
}

TEST(CommandBusTest, CloseUnblocksCallers) {
  CommandBus bus;
  std::thread t([&] { EXPECT_FALSE(bus.call("x", json::object(), 60000)["ok"]); });
  usleep(20000);
  bus.close();
  t.join();
}

TEST_F(ProjectTest, MoverCapaLlevaClipsParametrosEfectosYDeshace) {
  load(1, 1, kRed);
  load(3, 2, kBlue);
  params.set("/composition/layers/1/opacity", 0.25);
  params.set("/composition/layers/1/name", std::string("Fondo"));
  commands.run("effect.add", {{"entity", "/composition/layers/1"}, {"type", "hue"}});
  commands.run("structure.moveLayer", {{"layer", 1}, {"to", 3}});
  settle();
  EXPECT_EQ(params.getString(C(3, 1) + "/path"), kRed);
  EXPECT_EQ(params.getString(C(2, 2) + "/path"), kBlue);
  EXPECT_EQ(params.getString(C(1, 1) + "/path"), "");
  EXPECT_NEAR(params.getFloat("/composition/layers/3/opacity"), 0.25, 1e-6);
  EXPECT_NEAR(params.getFloat("/composition/layers/1/opacity"), 1, 1e-6);
  EXPECT_EQ(params.getString("/composition/layers/3/name"), "Fondo");
  EXPECT_FALSE(comp->effects().empty("/composition/layers/3"));
  EXPECT_TRUE(comp->effects().empty("/composition/layers/1"));
  commands.run("history.undo");
  settle();
  EXPECT_EQ(params.getString(C(1, 1) + "/path"), kRed);
  EXPECT_EQ(params.getString("/composition/layers/1/name"), "Fondo");
}

TEST_F(ProjectTest, QuitarCapaIntermediaYDeshacerRecuperaLaForma) {
  load(1, 1, kRed);
  load(2, 1, kBlue);
  load(3, 1, kRed);
  params.set("/composition/layers/3/opacity", 0.5);
  commands.run("structure.removeLayer", {{"layer", 2}});
  settle();
  EXPECT_EQ(comp->shape().layers, 2);
  EXPECT_EQ(params.getString(C(2, 1) + "/path"), kRed);
  EXPECT_NEAR(params.getFloat("/composition/layers/2/opacity"), 0.5, 1e-6);
  commands.run("history.undo");
  settle();
  EXPECT_EQ(comp->shape().layers, 3);
  EXPECT_EQ(params.getString(C(2, 1) + "/path"), kBlue);
  commands.run("history.redo");
  settle();
  EXPECT_EQ(comp->shape().layers, 2);
}

TEST_F(ProjectTest, InsertarYDuplicarCapasYColumnas) {
  load(2, 2, kBlue);
  params.set(C(2, 2) + "/name", std::string("Azul"));
  commands.run("structure.insertLayer", {{"layer", 1}});
  settle();
  EXPECT_EQ(comp->shape().layers, 4);
  EXPECT_EQ(params.getString(C(3, 2) + "/path"), kBlue);
  commands.run("structure.duplicateLayer", {{"layer", 3}});
  settle();
  EXPECT_EQ(params.getString(C(4, 2) + "/path"), kBlue);
  EXPECT_EQ(params.getString(C(3, 2) + "/path"), kBlue);
  commands.run("structure.insertColumn", {{"column", 1}});
  settle();
  EXPECT_EQ(comp->shape().columns, 5);
  EXPECT_EQ(params.getString(C(3, 3) + "/path"), kBlue);
  commands.run("structure.duplicateColumn", {{"column", 3}});
  settle();
  EXPECT_EQ(comp->shape().columns, 6);
  EXPECT_EQ(params.getString(C(3, 4) + "/path"), kBlue);
  EXPECT_EQ(params.getString(C(3, 4) + "/name"), "Azul");
  commands.run("structure.moveColumn", {{"column", 4}, {"to", 6}});
  settle();
  EXPECT_EQ(params.getString(C(3, 6) + "/path"), kBlue);
}

TEST_F(ProjectTest, LoQueSonabaVuelveASonarTrasReordenar) {
  load(1, 1, kRed);
  params.trigger(C(1, 1) + "/connect");
  settle();
  commands.run("structure.moveLayer", {{"layer", 1}, {"to", 2}});
  settle();
  settle();
  EXPECT_TRUE(params.getBool(C(2, 1) + "/connected"));
}

TEST_F(ProjectTest, UnaCarpetaLlamadaClipsNoSeReescribe) {
  const auto doc = json{{"shape", {{"layers", 2}, {"columns", 2}}}, {"values", json::object()}, {"persistent", json::object()},
                        {"decks", json::array({{{"cells", {{"0/1", {{"path", "/Users/a/clips/1/layers/2.mov"}, {"links", "/composition/layers/1/clips/2/speed"}}}}}}})}};
  const json out = project::remapDocument(doc, {1, 0}, {1, 0}, 2, 2);
  const json cell = out["decks"][0]["cells"]["1/0"];
  EXPECT_EQ(cell["path"], "/Users/a/clips/1/layers/2.mov");
  EXPECT_EQ(cell["links"], "/composition/layers/2/clips/1/speed");
}
