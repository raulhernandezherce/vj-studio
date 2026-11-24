#include <gtest/gtest.h>

#include "core/EventBus.h"

using namespace vj;

TEST(EventBus, DeliversByTopicAndWildcard) {
  EventBus bus;
  int exact = 0, all = 0;
  bus.subscribe("a", [&](const std::string&, const nlohmann::json& d) { if (d.is_object()) exact += d.value("n", 0); });
  auto id = bus.subscribe("", [&](const std::string&, const nlohmann::json&) { ++all; });
  bus.publish("a", {{"n", 2}});
  bus.publish("b");
  EXPECT_EQ(exact, 2);
  EXPECT_EQ(all, 2);
  bus.unsubscribe(id);
  bus.publish("a");
  EXPECT_EQ(all, 2);
}
