#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "output/OutputModule.h"
#include "patch/PatchModule.h"

using namespace vj;
using namespace vj::patch;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
compositor::EffectCatalog& catalog() {
  static compositor::EffectCatalog c;
  return c;
}
NodeLibrary& lib() {
  static NodeLibrary l(catalog());
  return l;
}
Node node(int id, const char* type, json values = json::object(), json attrs = json::object()) {
  Node n;
  n.id = id;
  n.type = type;
  n.name = type;
  n.values = std::move(values);
  n.attrs = std::move(attrs);
  return n;
}
json patchJson(const Patch& p) { return toJson(p); }
}  // namespace

TEST(PatchModel, EveryCategoryHasItsReferenceNodes) {
  std::map<std::string, int> count;
  for (const NodeType& t : lib().all()) ++count[t.category];
  for (const char* c : {"Entrada", "Salida", "Interfaz", "Matemáticas", "Comparación", "Texto", "Textura", "Efectos", "Fuentes"}) EXPECT_GT(count[c], 0) << c;
  EXPECT_GE(lib().all().size(), 30u);
  EXPECT_TRUE(lib().find("fx.blur")) << "los efectos del catálogo son nodos";
  EXPECT_FALSE(lib().find("fx.lut")) << "los que piden archivo aún no";
}

TEST(PatchModel, IncompatibleTypesAreRejected) {
  EXPECT_EQ(linkError(PortType::Float, PortType::Int), "");
  EXPECT_EQ(linkError(PortType::Float, PortType::String), "");
  EXPECT_NE(linkError(PortType::Texture, PortType::Float), "");
  EXPECT_NE(linkError(PortType::Color, PortType::Float), "");
  Patch p;
  p.nodes = {node(1, "tex.solid"), node(2, "math.add")};
  p.links = {{1, 2, "out", "a"}};
  EXPECT_THROW(validate(p, lib()), std::runtime_error);
  p.links = {{1, 2, "nada", "a"}};
  EXPECT_THROW(validate(p, lib()), std::runtime_error);
}

TEST(PatchModel, SaveFormatRoundTripsAndNewerVersionWarnsWithoutBreaking) {
  Patch p;
  p.name = "Prueba";
  p.nodes = {node(1, "data.float", {{"value", 2}}, {{"min", 0}, {"max", 4}})};
  p.nodes[0].hidden = {"value"};
  json j = toJson(p);
  EXPECT_EQ(toJson(fromJson(j, lib())), j);
  j["version"] = 99;
  j["nodes"].push_back(json{{"id", 2}, {"type", "del.futuro"}});
  std::vector<std::string> warnings;
  Patch later = fromJson(j, lib(), &warnings);
  EXPECT_EQ(warnings.size(), 2u);
  EXPECT_EQ(later.nodes.size(), 2u) << "el nodo desconocido se conserva";
  EXPECT_THROW(fromJson(json{{"format", "otro"}}, lib()), std::runtime_error);
}

TEST(PatchModel, ExposedParamsNeedARange) {
  Patch p;
  p.nodes = {node(1, "data.float", {{"value", 2}}, {{"min", 0}, {"max", 4}}), node(2, "data.float"), node(3, "data.trigger")};
  const auto e = exposedParams(p, lib());
  ASSERT_EQ(e.size(), 2u);
  EXPECT_EQ(e[0].node, 1);
  EXPECT_EQ(e[0].param.defaultValue, 2);
  EXPECT_EQ(e[1].param.type, "bool");
}

TEST(PatchModel, OrderCutsCyclesInsteadOfLooping) {
  Patch p;
  p.nodes = {node(1, "math.add"), node(2, "math.add"), node(3, "math.add")};
  p.links = {{1, 2, "out", "a"}, {2, 1, "out", "a"}, {2, 3, "out", "a"}};
  const Order o = evaluationOrder(p);
  EXPECT_EQ(o.nodes.size(), 3u);
  EXPECT_EQ(o.delayed.size(), 1u);
}

class PatchGpu : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_patch_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    params.defineAll(schema::parameters());
    params.set("/composition/width", int64_t{64});
    params.set("/composition/height", int64_t{36});
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    modules.add(std::make_unique<PatchModule>(dir.string()));
    output::OutputOptions o;
    o.hidden = true;
    o.documents = dir.string();
    modules.add(std::make_unique<output::OutputModule>(o));
    if (modules.startAll(ctx) != "") GTEST_SKIP() << "sin OpenGL";
    gpu = modules.get<output::OutputModule>("output")->backend();
    programs = std::make_unique<Programs>(*gpu);
    runtime = std::make_unique<Runtime>(lib(), params, dir.string());
    out = gpu->createTarget(64, 36);
  }
  void TearDown() override {
    if (gpu) {
      runtime->release(*gpu);
      programs.reset();
    }
    modules.stopAll();
    fs::remove_all(dir);
  }
  std::array<int, 4> run(const Patch& p, gfx::TextureId input = 0, int frames = 1, double t = 0) {
    validate(p, lib());
    runtime->update(p, *gpu);
    gfx::TextureId result = 0;
    for (int i = 0; i < frames; ++i) result = runtime->run(*gpu, *programs, {input, 64, 36, t + i / 60.0, 0, 120, 1 / 60.0});
    return result ? pixel(result) : std::array<int, 4>{-1, -1, -1, -1};
  }
  std::array<int, 4> pixel(gfx::TextureId tex) {
    gpu->beginPass(out, true);
    gfx::DrawCall d;
    d.program = programs->copy;
    d.textures[0] = tex;
    gpu->draw(d);
    const auto px = gpu->readback(out, 64, 36);
    const size_t i = (18 * 64 + 32) * 4;
    return {px[i], px[i + 1], px[i + 2], px[i + 3]};
  }
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
  gfx::IRenderBackend* gpu = nullptr;
  std::unique_ptr<Programs> programs;
  std::unique_ptr<Runtime> runtime;
  gfx::TargetId out = 0;
};

TEST_F(PatchGpu, InputConnectedToOutputReturnsImageUnchanged) {
  Patch solid;
  solid.nodes = {node(1, "tex.solid", {{"color", {0.2, 0.4, 0.6, 1}}}), node(2, "tex.out")};
  solid.links = {{1, 2, "out", "in"}};
  const auto a = run(solid);
  EXPECT_NEAR(a[0], 51, 1);
  EXPECT_NEAR(a[2], 153, 1);
  Patch pass;
  pass.nodes = {node(1, "tex.in"), node(2, "tex.out")};
  pass.links = {{1, 2, "out", "in"}};
  auto input = gpu->createTarget(64, 36);
  gpu->clearTarget(input, 0.1f, 0.5f, 0.9f, 1);
  const auto b = run(pass, gpu->targetTexture(input));
  EXPECT_NEAR(b[0], 26, 1);
  EXPECT_NEAR(b[1], 128, 1);
  EXPECT_NEAR(b[2], 230, 1);
  gpu->destroyTarget(input);
}

TEST_F(PatchGpu, NoOutputNodeMeansNothing) {
  Patch p;
  p.nodes = {node(1, "tex.solid")};
  EXPECT_EQ(run(p)[0], -1);
}

TEST_F(PatchGpu, EffectNodeMatchesTheEffect) {
  Patch p;
  p.nodes = {node(1, "tex.solid", {{"color", {1, 0, 0, 1}}}), node(2, "fx.invert"), node(3, "tex.out")};
  p.links = {{1, 2, "out", "in"}, {2, 3, "out", "in"}};
  const auto c = run(p);
  EXPECT_LT(c[0], 3);
  EXPECT_GT(c[1], 252);
  EXPECT_GT(c[2], 252);
}

TEST_F(PatchGpu, ValuesFlowThroughMathIntoColorlessNodes) {
  Patch p;
  p.nodes = {node(1, "data.float", {{"value", 0.25}}), node(2, "math.mul", {{"b", 2}}), node(3, "tex.solid", {{"color", {1, 1, 1, 1}}}), node(4, "tex.solid", {{"color", {0, 0, 0, 1}}}), node(5, "tex.mixer"), node(6, "tex.out")};
  p.links = {{1, 2, "out", "a"}, {2, 5, "out", "mix"}, {3, 5, "out", "a"}, {4, 5, "out", "b"}, {5, 6, "out", "in"}};
  EXPECT_NEAR(run(p)[0], 128, 2);
}

TEST_F(PatchGpu, BlendNodeUsesTheBlendMode) {
  Patch p;
  p.nodes = {node(1, "tex.solid", {{"color", {0.5, 0, 0, 1}}}), node(2, "tex.solid", {{"color", {0.25, 0, 0, 1}}}), node(3, "tex.blend", {}, {{"mode", "Add"}}), node(4, "tex.out")};
  p.links = {{1, 3, "out", "base"}, {2, 3, "out", "top"}, {3, 4, "out", "in"}};
  EXPECT_NEAR(run(p)[0], 191, 2);
}

TEST_F(PatchGpu, FeedbackLoopSettlesWithoutBlocking) {
  // Cada fotograma: 50 % de lo anterior + 50 % de blanco. Converge a blanco, sin colgarse.
  Patch p;
  p.nodes = {node(1, "tex.solid", {{"color", {1, 1, 1, 1}}}), node(2, "tex.mixer", {{"mix", 0.5}}), node(3, "tex.out")};
  p.links = {{2, 2, "out", "a"}, {1, 2, "out", "b"}, {2, 3, "out", "in"}};
  const auto one = run(p, 0, 1);
  EXPECT_NEAR(one[0], 128, 2) << "el primer fotograma parte de transparente";
  const auto start = std::chrono::steady_clock::now();
  const auto settled = run(p, 0, 30);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
  EXPECT_GT(settled[0], 250);
}

TEST_F(PatchGpu, ValueLoopRunsOnceAFrameAndStaysFinite) {
  Patch p;
  p.nodes = {node(1, "math.mul", {{"b", 1e6}}), node(2, "math.add", {{"b", 1}}), node(3, "tex.solid"), node(4, "tex.out")};
  p.links = {{2, 1, "out", "a"}, {1, 2, "out", "a"}, {3, 4, "out", "in"}};
  EXPECT_GE(run(p, 0, 200)[0], 0);
}

TEST_F(PatchGpu, BitDepthAttributeKeepsPrecision) {
  const auto chain = [](const char* depth) {
    Patch p;
    p.nodes = {node(1, "tex.solid", {{"color", {0.1, 0.1, 0.1, 1}}}, {{"depth", depth}}), node(2, "fx.exposure", {{"stops", -4}}, {{"depth", depth}}), node(3, "fx.exposure", {{"stops", 4}}, {{"depth", depth}}), node(4, "tex.out")};
    p.links = {{1, 2, "out", "in"}, {2, 3, "out", "in"}, {3, 4, "out", "in"}};
    return p;
  };
  Patch shallow = chain("8"), deep = chain("16");
  const int low = run(shallow)[0];
  runtime->release(*gpu);
  const int high = run(deep)[0];
  EXPECT_NEAR(high, 26, 1);
  EXPECT_GE(std::abs(low - 26), 3) << "con 8 bits se nota la pérdida";
}

TEST_F(PatchGpu, ResolutionAttributeChangesTheTarget) {
  Patch p;
  p.nodes = {node(1, "tex.solid", {{"color", {1, 0, 0, 1}}}, {{"width", 8}, {"height", 8}}), node(2, "tex.out")};
  p.links = {{1, 2, "out", "in"}};
  EXPECT_GT(run(p)[0], 250);
}

TEST_F(PatchGpu, ThousandInstancesDrawAndKeepRate) {
  Patch p;
  p.nodes = {node(1, "tex.shape", {{"color", {1, 1, 1, 1}}, {"size", 0.9}, {"spread", 1}}, {{"instances", 1000}}), node(2, "tex.out")};
  p.links = {{1, 2, "out", "in"}};
  const auto start = std::chrono::steady_clock::now();
  const auto c = run(p, 0, 60);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1)) << "60 fotogramas en menos de un segundo";
  const auto px = gpu->readback(out, 64, 36);
  int lit = 0, dark = 0;
  for (size_t i = 0; i < px.size(); i += 4) (px[i] > 128 ? lit : dark)++;
  EXPECT_GT(lit, 0);
  EXPECT_GT(dark, 0) << "se ven separados, no un bloque";
  (void)c;
}

TEST_F(PatchGpu, IsfNodeRunsAndBadCodeKeepsTheImage) {
  Patch p;
  p.nodes = {node(1, "tex.solid", {{"color", {0, 1, 0, 1}}}), node(2, "tex.isf", {}, {{"code", "/*{ \"INPUTS\": [] }*/\nvoid main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n"}}), node(3, "tex.out")};
  p.links = {{1, 2, "out", "in"}, {2, 3, "out", "in"}};
  EXPECT_GT(run(p)[0], 250);
  p.nodes[1].attrs["code"] = "/*{ \"INPUTS\": [] }*/\nesto no es glsl";
  const auto bad = run(p);
  EXPECT_GT(bad[1], 250) << "un nodo roto deja pasar su entrada";
  EXPECT_EQ(runtime->errors().size(), 1u);
}

TEST_F(PatchGpu, MissingMediaFileReportsOnNode) {
  Patch p;
  p.nodes = {node(1, "tex.media", {}, {{"path", "no-existe.mp4"}}), node(2, "tex.out")};
  p.links = {{1, 2, "out", "in"}};
  for (int i = 0; i < 200 && runtime->errors().empty(); ++i) {
    run(p);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_FALSE(runtime->errors().empty());
}

TEST_F(PatchGpu, StepSequencerFiresAClipThroughParameter) {
  params.set("/composition/layers/1/clips/1/path", std::string("vj://solid/00FF00"));
  auto* comp = modules.get<compositor::CompositionModule>("composition")->composition();
  for (int i = 0; i < 4; ++i) {
    comp->tick(0);
    comp->waitForLoads();
  }
  Patch p;
  p.nodes = {node(1, "seq.steps", {{"pattern", "1"}}), node(2, "out.trigger")};
  p.links = {{1, 2, "gate", "fire"}};
  validate(p, lib());
  runtime->update(p, *gpu);
  runtime->run(*gpu, *programs, {0, 64, 36, 0, 0.1, 120, 0.016});
  comp->tick(0);
  EXPECT_TRUE(params.getBool("/composition/layers/1/clips/1/connected"));
}

TEST_F(PatchGpu, SavedPatchIsARealEffectWithExposedParameter) {
  Patch p;
  p.name = "Oscurecer";
  p.category = "effect";
  p.nodes = {node(1, "tex.in"), node(2, "data.float", {{"value", 1}}, {{"min", 0}, {"max", 1}}), node(3, "tex.solid", {{"color", {0, 0, 0, 1}}}), node(4, "tex.mixer"), node(5, "tex.out")};
  p.nodes[1].name = "Cantidad";
  p.links = {{1, 4, "out", "a"}, {3, 4, "out", "b"}, {2, 4, "out", "mix"}, {4, 5, "out", "in"}};
  commands.run("patch.set", {{"patch", patchJson(p)}});
  commands.run("patch.save", json::object());
  EXPECT_EQ(commands.run("patch.list")[0], "Oscurecer");
  auto* comp = modules.get<compositor::CompositionModule>("composition")->composition();
  ASSERT_TRUE(comp->effects().catalog().find("patch:Oscurecer"));
  params.set("/composition/layers/1/clips/1/path", std::string("vj://solid/FF0000"));
  for (int i = 0; i < 4; ++i) {
    comp->tick(0);
    comp->waitForLoads();
  }
  params.trigger("/composition/layers/1/clips/1/connect");
  params.set("/composition/layers/1/blend", std::string("Alpha"));
  const int id = comp->effects().add("/composition/layers/1", "patch:Oscurecer");
  const std::string inst = comp->effects().instancePath("/composition/layers/1", id);
  const auto red = [&] {
    modules.tickAll(0);
    auto* o = modules.get<output::OutputModule>("output");
    gfx::TargetId t = gpu->createTarget(64, 36);
    gpu->beginPass(t, true);
    gfx::DrawCall d;
    d.program = programs->copy;
    d.textures[0] = o->compositionTexture();
    gpu->draw(d);
    const auto px = gpu->readback(t, 64, 36);
    gpu->destroyTarget(t);
    return static_cast<int>(px[(18 * 64 + 32) * 4]);
  };
  EXPECT_LT(red(), 5) << "cantidad 1: negro";
  ASSERT_EQ(params.set(inst + "/n2", 0.0), ParamError::None);
  EXPECT_GT(red(), 250) << "cantidad 0: el parámetro expuesto gobierna el parche";
}

TEST_F(PatchGpu, BadLinkNeverReplacesTheRunningPatch) {
  Patch good;
  good.nodes = {node(1, "tex.solid")};
  commands.run("patch.set", {{"patch", patchJson(good)}});
  Patch bad = good;
  bad.nodes.push_back(node(2, "math.add"));
  bad.links = {{1, 2, "out", "a"}};
  EXPECT_THROW(commands.run("patch.set", {{"patch", patchJson(bad)}}), std::exception);
  EXPECT_EQ(commands.run("patch.get")["nodes"].size(), 1u);
}

TEST_F(PatchGpu, TooManyExposedParamsRefuseToSave) {
  Patch p;
  p.name = "Grande";
  p.category = "effect";
  for (int i = 1; i <= 13; ++i) p.nodes.push_back(node(i, "data.float", {}, {{"min", 0}, {"max", 1}}));
  commands.run("patch.set", {{"patch", patchJson(p)}});
  EXPECT_THROW(commands.run("patch.save"), std::runtime_error);
  EXPECT_FALSE(fs::exists(dir / "Patches" / "Grande.vjpatch"));
}

TEST_F(PatchGpu, ConsolidateCopiesMediaAndWorksElsewhere) {
  const auto media = dir / "origen.png";
  std::ofstream(media) << "x";
  Patch p;
  p.name = "Con medio";
  p.nodes = {node(1, "tex.media", {}, {{"path", media.string()}})};
  commands.run("patch.set", {{"patch", patchJson(p)}});
  commands.run("patch.save");
  const json r = commands.run("patch.consolidate", {{"name", "Con medio"}});
  EXPECT_EQ(r["copied"], 1);
  fs::remove(media);
  EXPECT_TRUE(fs::exists(dir / "Patches" / "Con medio" / "origen.png"));
  std::ifstream in(r["file"].get<std::string>());
  EXPECT_EQ(json::parse(in)["nodes"][0]["attrs"]["path"], "origen.png");
}

TEST_F(PatchGpu, BadNamesCannotEscapeTheFolder) {
  commands.run("patch.set", {{"patch", patchJson(Patch{})}});
  EXPECT_THROW(commands.run("patch.save", {{"name", "../fuera"}}), std::runtime_error);
}
