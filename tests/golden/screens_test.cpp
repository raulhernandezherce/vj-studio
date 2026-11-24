#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "output/OutputModule.h"

using namespace vj;
using nlohmann::json;
namespace fs = std::filesystem;

class Screens : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_screens_" + std::to_string(getpid()));
    fs::remove_all(dir);
    params.defineAll(schema::parameters());
    params.set("/composition/width", int64_t{320});
    params.set("/composition/height", int64_t{180});
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{3, 2, 1}));
    output::OutputOptions o;
    o.hidden = true;
    o.documents = dir.string();
    modules.add(std::make_unique<output::OutputModule>(o));
    if (modules.startAll(ctx) != "") GTEST_SKIP() << "sin OpenGL";
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
    for (int l = 1; l <= 3; ++l) params.set("/composition/layers/" + std::to_string(l) + "/blend", std::string("Alpha"));
  }
  void TearDown() override {
    modules.stopAll();
    fs::remove_all(dir);
  }
  std::string C(int l) { return "/composition/layers/" + std::to_string(l) + "/clips/1"; }
  void clip(int l, const std::string& path) {
    params.set(C(l) + "/path", path);
    params.set(C(l) + "/resize", std::string("Stretch"));
    for (int i = 0; i < 3; ++i) {
      comp->tick(0);
      comp->waitForLoads();
    }
    params.trigger(C(l) + "/connect");
    frame();
  }
  void frame() { modules.tickAll(0); }
  // Mitad izquierda roja y derecha azul, sin tocar la composición: dos capas enrutadas no.
  void splitComposition() {
    clip(1, "vj://solid/FF0000");
    clip(2, "vj://solid/0000FF");
    params.set(C(2) + "/transform/scalewidth", 0.5);
    params.set(C(2) + "/transform/x", 80.0);
    frame();
  }
  json blankSlice(int id) {
    return {{"id", id}, {"name", "S"}, {"sourceType", "composition"}};
  }
  json screen(int id, const std::vector<json>& slices, int w = 320, int h = 180) {
    return {{"id", id}, {"name", "P" + std::to_string(id)}, {"device", "virtual"}, {"width", w}, {"height", h}, {"slices", json(slices)}};
  }
  void setup(const std::vector<json>& screens) {
    commands.run("screens.set", {{"setup", {{"format", "vjstudio-screens"}, {"version", 1}, {"name", "T"}, {"screens", json(screens)}}}});
    frame();
    frame();
  }
  std::array<int, 4> pixel(int id, double x, double y) {
    const json p = commands.run("screens.pixels", {{"id", id}, {"x", x}, {"y", y}});
    return {p[0], p[1], p[2], p[3]};
  }
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
  compositor::Composition* comp = nullptr;
};

TEST_F(Screens, TwoScreensShowDifferentPartsOfTheComposition) {
  splitComposition();
  json left = blankSlice(1), right = blankSlice(1);
  left["inW"] = 0.5;
  right["inX"] = 0.5;
  right["inW"] = 0.5;
  setup({screen(1, {left}), screen(2, {right})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240);
  EXPECT_LT(pixel(1, 0.5, 0.5)[2], 15);
  EXPECT_GT(pixel(2, 0.5, 0.5)[2], 240);
  EXPECT_LT(pixel(2, 0.5, 0.5)[0], 15);
}

TEST_F(Screens, BrightnessOfOneScreenDoesNotAffectAnother) {
  clip(1, "vj://solid/FFFFFF");
  json a = screen(1, {blankSlice(1)}), b = screen(2, {blankSlice(1)});
  a["brightness"] = 0.5;
  a["red"] = 0.0;
  setup({a, b});
  EXPECT_LT(pixel(1, 0.5, 0.5)[0], 5);
  EXPECT_NEAR(pixel(1, 0.5, 0.5)[1], 128, 3);
  EXPECT_GT(pixel(2, 0.5, 0.5)[0], 250);
}

TEST_F(Screens, MovingTheInputChangesTheShownZone) {
  splitComposition();
  json s = blankSlice(1);
  s["inW"] = 0.25;
  setup({screen(1, {s})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240);
  s["inX"] = 0.7;
  setup({screen(1, {s})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[2], 240);
}

TEST_F(Screens, CornersPlaceTheSliceOnTheScreen) {
  clip(1, "vj://solid/FFFFFF");
  json s = blankSlice(1);
  s["corners"] = {{0.5, 0.5}, {1, 0.5}, {1, 1}, {0.5, 1}};
  setup({screen(1, {s})});
  EXPECT_EQ(pixel(1, 0.25, 0.25)[0], 0);
  EXPECT_GT(pixel(1, 0.75, 0.75)[0], 250);
}

TEST_F(Screens, TopOfCompositionStaysOnTop) {
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/0000FF");
  params.set(C(2) + "/transform/scaleheight", 0.5);
  params.set(C(2) + "/transform/y", -45.0);  // mitad superior azul
  frame();
  setup({screen(1, {blankSlice(1)})});
  EXPECT_GT(pixel(1, 0.5, 0.2)[2], 240);
  EXPECT_GT(pixel(1, 0.5, 0.8)[0], 240);
}

TEST_F(Screens, FlipMirrorsTheImage) {
  splitComposition();
  json s = blankSlice(1);
  s["flipH"] = true;
  setup({screen(1, {s})});
  EXPECT_GT(pixel(1, 0.1, 0.5)[2], 240);
  EXPECT_GT(pixel(1, 0.9, 0.5)[0], 240);
}

TEST_F(Screens, HexagonPolygonCutsTheOutput) {
  clip(1, "vj://solid/FFFFFF");
  json s = blankSlice(1);
  s["polygon"] = {{0.25, 0}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0.25, 1}, {0, 0.5}};
  setup({screen(1, {s})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 250);
  EXPECT_EQ(pixel(1, 0.02, 0.02)[0], 0);
  EXPECT_EQ(pixel(1, 0.98, 0.98)[0], 0);
}

TEST_F(Screens, EdgeBlendOverlapIsUniform) {
  clip(1, "vj://solid/FFFFFF");
  // Dos proyectores con 20 % de solape: en la zona común la suma debe seguir siendo 1.
  json a = blankSlice(1), b = blankSlice(2);
  a["corners"] = {{0, 0}, {0.6, 0}, {0.6, 1}, {0, 1}};
  b["corners"] = {{0.4, 0}, {1, 0}, {1, 1}, {0.4, 1}};
  a["inW"] = 0.6;
  b["inX"] = 0.4;
  b["inW"] = 0.6;
  for (json* s : {&a, &b}) {
    (*s)["blend"] = true;
    (*s)["blendGamma"] = 1.0;
  }
  a["blendRight"] = 0.2 / 0.6;
  b["blendLeft"] = 0.2 / 0.6;
  const auto contribution = [&](const json& one, double x) {
    setup({screen(1, {one})});
    return pixel(1, x, 0.5)[0];
  };
  for (double x : {0.42, 0.45, 0.5, 0.55, 0.58}) {
    const int sum = contribution(a, x) + contribution(b, x);
    EXPECT_NEAR(sum / 255.0, 1.0, 0.05) << x;
  }
  setup({screen(1, {a, b})});
  EXPECT_GT(pixel(1, 0.1, 0.5)[0], 250);
  EXPECT_GT(pixel(1, 0.9, 0.5)[0], 250);
}

TEST_F(Screens, RoutedLayerAppearsOnlyInItsSlice) {
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/00FF00");
  json routed = blankSlice(1), full = blankSlice(1);
  routed["sourceType"] = "layer";
  routed["sourceIndex"] = 2;
  setup({screen(1, {routed}), screen(2, {full})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[1], 240);
  EXPECT_LT(pixel(1, 0.5, 0.5)[0], 15);
  // La capa 2 estaba encima: al salir de la mezcla, la composición vuelve a ser roja.
  EXPECT_GT(pixel(2, 0.5, 0.5)[0], 240);
  EXPECT_LT(pixel(2, 0.5, 0.5)[1], 15);
}

TEST_F(Screens, RoutedLayerClearsWhenEjected) {
  clip(2, "vj://solid/00FF00");
  json routed = blankSlice(1);
  routed["sourceType"] = "layer";
  routed["sourceIndex"] = 2;
  setup({screen(1, {routed})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[1], 240);
  params.trigger("/composition/layers/2/eject");
  comp->tick(0);
  frame();
  EXPECT_EQ(pixel(1, 0.5, 0.5)[1], 0);
}

TEST_F(Screens, IgnoreBypassKeepsRoutedLayerVisible) {
  clip(1, "vj://solid/00FF00");
  params.set("/composition/layers/1/bypass", true);
  json routed = blankSlice(1);
  routed["sourceType"] = "layer";
  routed["sourceIndex"] = 1;
  setup({screen(1, {routed})});
  EXPECT_EQ(pixel(1, 0.5, 0.5)[1], 0);
  routed["ignoreBypass"] = true;
  setup({screen(1, {routed})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[1], 240);
}

TEST_F(Screens, ScreenAsSourceAndCycleStaysBlack) {
  clip(1, "vj://solid/FFFFFF");
  json fromScreen = blankSlice(1);
  fromScreen["sourceType"] = "screen";
  fromScreen["sourceIndex"] = 2;
  json from1 = fromScreen;
  from1["sourceIndex"] = 1;
  json dim = screen(2, {blankSlice(1)});
  dim["brightness"] = 0.5;
  setup({screen(1, {fromScreen}), dim});
  EXPECT_NEAR(pixel(1, 0.5, 0.5)[0], 128, 3) << "hereda el ajuste de la pantalla origen";
  setup({screen(1, {fromScreen}), screen(2, {from1})});  // ciclo
  EXPECT_NO_THROW(pixel(1, 0.5, 0.5));
}

TEST_F(Screens, MasksHideAndInvert) {
  clip(1, "vj://solid/FFFFFF");
  json hide = screen(1, {blankSlice(1)});
  hide["masks"] = {{{"points", {{0, 0}, {0.5, 0}, {0.5, 1}, {0, 1}}}, {"invert", true}}};
  setup({hide});
  EXPECT_EQ(pixel(1, 0.25, 0.5)[0], 0);
  EXPECT_GT(pixel(1, 0.75, 0.5)[0], 250);
  hide["masks"][0]["invert"] = false;
  setup({hide});
  EXPECT_GT(pixel(1, 0.25, 0.5)[0], 250);
  EXPECT_EQ(pixel(1, 0.75, 0.5)[0], 0);
}

TEST_F(Screens, SliceMaskCutsOnlyItsSliceAndFollowsTheCorners) {
  clip(1, "vj://solid/FFFFFF");
  json left = blankSlice(1), right = blankSlice(2);
  left["corners"] = {{0, 0}, {0.5, 0}, {0.5, 1}, {0, 1}};
  right["corners"] = {{0.5, 0}, {1, 0}, {1, 1}, {0.5, 1}};
  left["masks"] = {{{"points", {{0, 0}, {0.5, 0}, {0.5, 0.5}, {0, 0.5}}}, {"invert", false}}};
  setup({screen(1, {left, right})});
  EXPECT_GT(pixel(1, 0.1, 0.25)[0], 250) << "dentro de la máscara";
  EXPECT_EQ(pixel(1, 0.1, 0.75)[0], 0) << "debajo de la máscara: y no está invertida";
  EXPECT_EQ(pixel(1, 0.4, 0.25)[0], 0) << "a la derecha de la máscara";
  EXPECT_GT(pixel(1, 0.75, 0.75)[0], 250) << "el otro slice no se recorta";
  left["masks"][0]["invert"] = true;
  setup({screen(1, {left, right})});
  EXPECT_EQ(pixel(1, 0.1, 0.25)[0], 0);
  EXPECT_GT(pixel(1, 0.1, 0.75)[0], 250);
  left["masks"] = json::array();
  setup({screen(1, {left, right})});
  EXPECT_GT(pixel(1, 0.1, 0.25)[0], 250) << "quitar la máscara devuelve el slice entero";
}

TEST_F(Screens, SmoothMaskCurvesThroughItsPoints) {
  clip(1, "vj://solid/FFFFFF");
  json s = screen(1, {blankSlice(1)});
  s["masks"] = {{{"points", {{0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}}}, {"invert", false}, {"smooth", false}}};
  setup({s});
  EXPECT_EQ(pixel(1, 0.5, 0.16)[0], 0) << "lado recto";
  s["masks"][0]["smooth"] = true;
  setup({s});
  EXPECT_GT(pixel(1, 0.5, 0.16)[0], 250) << "el lado se curva hacia fuera";
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 250);
}

TEST_F(Screens, TestCardDiagonalContinuesAcrossScreens) {
  json left = blankSlice(1), right = blankSlice(1);
  left["inW"] = 0.5;
  right["inX"] = 0.5;
  right["inW"] = 0.5;
  setup({screen(1, {left}), screen(2, {right})});
  params.set("/output/testcard", true);
  frame();
  // La diagonal pasa por (0,0)-(1,1): llega al borde derecho de una a media altura,
  // y sale del borde izquierdo de la otra a la misma altura.
  const auto isDiagonal = [&](int id, double x, double y) {
    const auto p = pixel(id, x, y);
    return p[0] > 200 && p[1] < 120;
  };
  EXPECT_TRUE(isDiagonal(1, 0.5, 0.25));
  EXPECT_TRUE(isDiagonal(1, 0.99, 0.495));
  EXPECT_TRUE(isDiagonal(2, 0.01, 0.505));
  EXPECT_TRUE(isDiagonal(2, 0.5, 0.75));
}

TEST_F(Screens, IdentifyDrawsOverTheScreen) {
  clip(1, "vj://solid/000000");
  setup({screen(1, {blankSlice(1)})});
  EXPECT_EQ(pixel(1, 0.05, 0.05)[1], 0);
  params.set("/output/identify", true);
  frame();
  const auto p = pixel(1, 0.05, 0.05);
  EXPECT_GT(p[0] + p[1] + p[2], 10);
}

TEST_F(Screens, ScenarioSavesLoadsAndIsIndependentOfComposition) {
  json left = blankSlice(1);
  left["inW"] = 0.5;
  setup({screen(7, {left})});
  commands.run("screens.preset.save", {{"name", "Sala Grande"}});
  EXPECT_EQ(params.getString("/output/setup"), "Sala Grande");
  setup({screen(1, {blankSlice(1)})});
  EXPECT_EQ(commands.run("screens.preset.list").size(), 1u);
  commands.run("screens.preset.load", {{"name", "Sala Grande"}});
  EXPECT_EQ(commands.run("screens.get")["screens"][0]["id"], 7);
  EXPECT_EQ(commands.run("screens.get")["screens"][0]["slices"][0]["inW"], 0.5);
  EXPECT_THROW(commands.run("screens.preset.load", {{"name", "No existe"}}), std::runtime_error);
  EXPECT_THROW(commands.run("screens.preset.save", {{"name", "../fuera"}}), std::runtime_error) ;
}

TEST_F(Screens, UndoHasItsOwnHistory) {
  setup({screen(1, {blankSlice(1)})});
  setup({screen(2, {blankSlice(1)})});
  commands.run("screens.undo");
  EXPECT_EQ(commands.run("screens.get")["screens"][0]["id"], 1);
  commands.run("screens.redo");
  EXPECT_EQ(commands.run("screens.get")["screens"][0]["id"], 2);
}

TEST_F(Screens, SnapshotStillCapturesTheComposition) {
  clip(1, "vj://solid/FF0000");
  const auto png = dir / "captura.png";
  commands.run("output.capture", {{"path", png.string()}});
  EXPECT_GT(fs::file_size(png), 0u);
}

TEST_F(Screens, SetupSurvivesRestart) {
  setup({screen(9, {blankSlice(1)})});
  EXPECT_TRUE(fs::exists(dir / "Preferences" / "screens.json"));
}

TEST_F(Screens, CompositionResizeKeepsNormalizedMap) {
  splitComposition();
  json left = blankSlice(1);
  left["inW"] = 0.5;
  setup({screen(1, {left})});
  params.set("/composition/width", int64_t{640});
  params.set("/composition/height", int64_t{360});
  frame();
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240);
}

TEST_F(Screens, BusShowsItsSideWhateverTheCrossfader) {
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/00FF00");
  params.set("/composition/layers/1/crossfaderbus", std::string("A"));
  params.set("/composition/layers/2/crossfaderbus", std::string("B"));
  params.set("/composition/crossfader/phase", 0.0);
  json a = blankSlice(1), b = blankSlice(1);
  a["sourceType"] = "bus";
  a["sourceIndex"] = 1;
  b["sourceType"] = "bus";
  b["sourceIndex"] = 2;
  setup({screen(1, {a}), screen(2, {b})});
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240);
  EXPECT_LT(pixel(1, 0.5, 0.5)[1], 15);
  EXPECT_GT(pixel(2, 0.5, 0.5)[1], 240);
  EXPECT_LT(pixel(2, 0.5, 0.5)[0], 15);
}

TEST_F(Screens, DelayHoldsTheOldImageThenCatchesUp) {
  clip(1, "vj://solid/FF0000");
  json s = screen(1, {blankSlice(1)});
  s["delay"] = 50.0;
  setup({s});
  for (int i = 0; i < 8; ++i) modules.tickAll(1.0 / 60);
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240);
  params.set(C(1) + "/path", std::string("vj://solid/00FF00"));
  for (int i = 0; i < 3; ++i) {
    comp->tick(0);
    comp->waitForLoads();
  }
  params.trigger(C(1) + "/connect");
  modules.tickAll(1.0 / 60);
  EXPECT_GT(pixel(1, 0.5, 0.5)[0], 240) << "aún debe verse el fotograma antiguo";
  for (int i = 0; i < 6; ++i) modules.tickAll(1.0 / 60);
  EXPECT_GT(pixel(1, 0.5, 0.5)[1], 240);
  EXPECT_LT(pixel(1, 0.5, 0.5)[0], 15);
}

TEST_F(Screens, OutlineOnlyDrawsTheEdgeNotTheContent) {
  clip(1, "vj://solid/FF0000");
  json s = blankSlice(1);
  s["outlineOnly"] = true;
  s["outlineWidth"] = 0.04;
  setup({screen(1, {s})});
  EXPECT_EQ(pixel(1, 0.5, 0.5)[0], 0);
  EXPECT_GT(pixel(1, 0.5, 0.005)[1], 240);
}

TEST_F(Screens, ContentMovesInsideTheSliceWithoutMovingTheShape) {
  splitComposition();
  json plain = blankSlice(1), moved = blankSlice(1);
  moved["contentX"] = 0.5;
  setup({screen(1, {plain}), screen(2, {moved})});
  EXPECT_GT(pixel(1, 0.75, 0.5)[2], 240);
  EXPECT_GT(pixel(2, 0.75, 0.5)[0], 240);
}

TEST_F(Screens, BlendGammaPerChannelTintsOnlyTheOverlap) {
  clip(1, "vj://solid/FFFFFF");
  json s = blankSlice(1);
  s["blend"] = true;
  s["blendLeft"] = 0.5;
  s["blendGammaRed"] = 2.0;
  setup({screen(1, {s})});
  const auto fading = pixel(1, 0.25, 0.5), plain = pixel(1, 0.75, 0.5);
  EXPECT_LT(fading[0], fading[1] - 10) << "el rojo cae más que el verde en el solape";
  EXPECT_NEAR(plain[0], plain[1], 2) << "fuera del solape no cambia";
}
