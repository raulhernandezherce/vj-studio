#include <gtest/gtest.h>

#include "core/ParameterRegistry.h"
#include "core/generated/BlendModes.gen.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;

TEST(Schema, LoadsAndEveryDefaultIsInRange) {
  ParameterRegistry r;
  r.defineAll(schema::parameters());
  for (const auto& d : schema::parameters()) {
    ASSERT_EQ(d.path.front(), '/') << d.path;
    EXPECT_FALSE(d.milestone.empty()) << d.path;
    if (d.type == ParamType::Float) {
      const double v = std::get<double>(d.defaultValue);
      EXPECT_GE(v, d.min) << d.path;
      EXPECT_LE(v, d.max) << d.path;
    }
  }
}

TEST(Schema, EnumDefaultsAreOptions) {
  for (const auto* table : {&schema::parameters(), &schema::layerParameters(), &schema::clipParameters(), &schema::groupParameters()})
    for (const auto& d : *table)
      if (d.type == ParamType::Enum) {
        const auto& v = std::get<std::string>(d.defaultValue);
        EXPECT_NE(std::find(d.options.begin(), d.options.end(), v), d.options.end()) << d.path;
      }
}

TEST(Schema, TwentyTwoBlendModesWithAddFirst) {
  EXPECT_EQ(schema::blendModes().size(), 22u);
  EXPECT_STREQ(schema::blendModes()[0].name, "Add");
  for (const auto& d : schema::layerParameters())
    if (d.path == "blend") EXPECT_EQ(std::get<std::string>(d.defaultValue), "Add");
}
