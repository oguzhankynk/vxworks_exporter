#include <gtest/gtest.h>

#include <string>

#include "core/exposition_check.hpp"

namespace vxexp {
namespace {

TEST(ExpositionCheckTest, AcceptsValidText) {
  const ExpositionCheck c = checkExposition(
      "# HELP a_total A.\n# TYPE a_total counter\na_total{x=\"1\"} 2\na_total{x=\"2\"} 3.5\n"
      "\n# some comment\n# TYPE b gauge\nb NaN\nb{y=\"\"} +Inf 1700000000\n");
  ASSERT_TRUE(c.ok) << c.error;
  EXPECT_TRUE(c.hasFamily("a_total"));
  EXPECT_TRUE(c.hasFamily("b"));
  EXPECT_FALSE(c.hasFamily("c"));
  ASSERT_EQ(c.samples.size(), 4U);
  EXPECT_EQ(c.samples[1].labels.at("x"), "2");
  EXPECT_DOUBLE_EQ(c.samples[1].value, 3.5);
}

TEST(ExpositionCheckTest, AcceptsCounterSuffixOnSamples) {
  const ExpositionCheck c = checkExposition("# TYPE req counter\nreq_total 1\n");
  ASSERT_TRUE(c.ok) << c.error;
}

TEST(ExpositionCheckTest, UnescapesLabelValues) {
  const ExpositionCheck c = checkExposition("m{v=\"a\\\\b\\\"c\\nd\"} 1\n");
  ASSERT_TRUE(c.ok) << c.error;
  EXPECT_EQ(c.samples[0].labels.at("v"), "a\\b\"c\nd");
}

struct BadCase {
  const char* text;
  std::size_t line;
};

class ExpositionCheckRejects : public ::testing::TestWithParam<BadCase> {};

TEST_P(ExpositionCheckRejects, ReportsTheLine) {
  const ExpositionCheck c = checkExposition(GetParam().text);
  EXPECT_FALSE(c.ok);
  EXPECT_EQ(c.line, GetParam().line) << c.error;
  EXPECT_FALSE(c.error.empty());
}

INSTANTIATE_TEST_SUITE_P(
    BadInput,
    ExpositionCheckRejects,
    ::testing::Values(BadCase{"m 1", 1},                                    // no final newline
                      BadCase{"1m 1\n", 1},                                 // bad name
                      BadCase{"m{1x=\"a\"} 1\n", 1},                        // bad label name
                      BadCase{"m{a:b=\"a\"} 1\n", 1},                       // colon in label
                      BadCase{"m{a=b} 1\n", 1},                             // unquoted value
                      BadCase{"m{a=\"b} 1\n", 1},                           // unterminated
                      BadCase{"m{a=\"\\t\"} 1\n", 1},                       // bad escape
                      BadCase{"m{a=\"x\",a=\"y\"} 1\n", 1},                 // duplicate label
                      BadCase{"m{a=\"x\" 1\n", 1},                          // unterminated set
                      BadCase{"m one\n", 1},                                // bad value
                      BadCase{"m\n", 1},                                    // no value
                      BadCase{"m 1 later\n", 1},                            // bad timestamp
                      BadCase{"# TYPE m\n", 1},                             // no type
                      BadCase{"# TYPE m histogramx\n", 1},                  // unknown type
                      BadCase{"# TYPE \n", 1},                              // no name
                      BadCase{"# TYPE m gauge\n# TYPE m gauge\n", 2},       // second TYPE
                      BadCase{"m 1\n# TYPE m gauge\n", 2},                  // TYPE after sample
                      BadCase{"a 1\nb 1\na 2\n", 3},                        // family split
                      BadCase{"# TYPE a gauge\nb 1\n# HELP a again\n", 3},  // family reopened
                      BadCase{"m{a=\"\\", 1}));                             // dangling escape

}  // namespace
}  // namespace vxexp
