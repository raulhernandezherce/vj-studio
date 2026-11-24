#include <gtest/gtest.h>

#include "core/EventBus.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRegistry.h"

using namespace vj;

namespace {
class Fake final : public IModule {
 public:
  Fake(std::string n, std::vector<std::string>& log, bool ok = true) : n_(std::move(n)), log_(log), ok_(ok) {}
  std::string name() const override { return n_; }
  bool start(EngineContext&) override { log_.push_back("start " + n_); return ok_; }
  void tick(double) override { log_.push_back("tick " + n_); }
  void stop() override { log_.push_back("stop " + n_); }

 private:
  std::string n_;
  std::vector<std::string>& log_;
  bool ok_;
};
}  // namespace

TEST(ModuleRegistry, OrderStartTickStopReverse) {
  ParameterRegistry p; EventBus e; ModuleRegistry m; EngineContext c{p, e, m};
  std::vector<std::string> log;
  m.add(std::make_unique<Fake>("a", log));
  m.add(std::make_unique<Fake>("b", log));
  EXPECT_EQ(m.startAll(c), "");
  m.tickAll(0.1);
  m.stopAll();
  EXPECT_EQ(log, (std::vector<std::string>{"start a", "start b", "tick a", "tick b", "stop b", "stop a"}));
}

TEST(ModuleRegistry, FailedStartStopsThoseAlreadyStarted) {
  ParameterRegistry p; EventBus e; ModuleRegistry m; EngineContext c{p, e, m};
  std::vector<std::string> log;
  m.add(std::make_unique<Fake>("a", log));
  m.add(std::make_unique<Fake>("b", log, false));
  EXPECT_EQ(m.startAll(c), "b");
  EXPECT_EQ(log.back(), "stop a");
}

TEST(ModuleRegistry, RejectsDuplicates) {
  ModuleRegistry m;
  std::vector<std::string> log;
  m.add(std::make_unique<Fake>("a", log));
  EXPECT_THROW(m.add(std::make_unique<Fake>("a", log)), std::logic_error);
}
