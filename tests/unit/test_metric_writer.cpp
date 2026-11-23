#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "core/exposition_check.hpp"
#include "core/metric_writer.hpp"
#include "core/sink.hpp"

namespace vxexp {
namespace {

TEST(MetricWriterTest, WritesFamilyHeaderAndSample) {
  StringSink sink;
  MetricWriter out(sink);
  out.family("vxworks_tasks", "Tasks by state.", MetricType::Gauge);
  out.sample("vxworks_tasks", {{"state", "ready"}}, std::int64_t{3});
  EXPECT_EQ(sink.text(),
            "# HELP vxworks_tasks Tasks by state.\n"
            "# TYPE vxworks_tasks gauge\n"
            "vxworks_tasks{state=\"ready\"} 3\n");
}

TEST(MetricWriterTest, SingleWritesUnlabelledCounter) {
  StringSink sink;
  MetricWriter out(sink);
  out.single("vxexporter_scrapes_total", "Scrapes.", MetricType::Counter, std::uint64_t{7});
  EXPECT_EQ(sink.text(),
            "# HELP vxexporter_scrapes_total Scrapes.\n"
            "# TYPE vxexporter_scrapes_total counter\n"
            "vxexporter_scrapes_total 7\n");
}

TEST(MetricWriterTest, SeparatesLabelsWithCommas) {
  StringSink sink;
  MetricWriter out(sink);
  out.sample("m", {{"a", "1"}, {"b", "2"}, {"c", "3"}}, std::uint64_t{0});
  EXPECT_EQ(sink.text(), "m{a=\"1\",b=\"2\",c=\"3\"} 0\n");
}

TEST(MetricWriterTest, EscapesLabelValues) {
  StringSink sink;
  MetricWriter out(sink);
  out.sample("m", {{"path", "C:\\dir \"x\"\nnext"}}, std::uint64_t{1});
  EXPECT_EQ(sink.text(), "m{path=\"C:\\\\dir \\\"x\\\"\\nnext\"} 1\n");
}

TEST(MetricWriterTest, EscapesHelpButNotQuotes) {
  StringSink sink;
  MetricWriter out(sink);
  out.family("m", "a \"quoted\" back\\slash\nnewline", MetricType::Gauge);
  EXPECT_EQ(sink.text(), "# HELP m a \"quoted\" back\\\\slash\\nnewline\n# TYPE m gauge\n");
}

TEST(MetricWriterTest, NullLabelValueIsEmpty) {
  StringSink sink;
  MetricWriter out(sink);
  out.sample("m", {{"x", nullptr}}, std::uint64_t{1});
  EXPECT_EQ(sink.text(), "m{x=\"\"} 1\n");
}

TEST(MetricWriterTest, WritesIntegersExactly) {
  StringSink sink;
  MetricWriter out(sink);
  out.sample("u", {}, std::numeric_limits<std::uint64_t>::max());
  out.sample("s", {}, std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(sink.text(), "u 18446744073709551615\ns -9223372036854775808\n");
}

TEST(FormatDoubleTest, FormatsSpecialValues) {
  char text[40];
  EXPECT_STREQ(formatDouble(std::nan(""), text, sizeof text), "NaN");
  EXPECT_STREQ(formatDouble(std::numeric_limits<double>::infinity(), text, sizeof text), "+Inf");
  EXPECT_STREQ(formatDouble(-std::numeric_limits<double>::infinity(), text, sizeof text), "-Inf");
}

TEST(FormatDoubleTest, FormatsOrdinaryValuesCompactly) {
  char text[40];
  EXPECT_STREQ(formatDouble(3.02, text, sizeof text), "3.02");
  EXPECT_STREQ(formatDouble(0.0, text, sizeof text), "0");
  EXPECT_STREQ(formatDouble(123456.000001, text, sizeof text), "123456.000001");
}

TEST(MetricWriterTest, OutputPassesTheFormatCheck) {
  StringSink sink;
  MetricWriter out(sink);
  out.family("vxworks_task_cpu_seconds_total", "CPU \"time\"\\.", MetricType::Counter);
  out.sample("vxworks_task_cpu_seconds_total", {{"task", "t\"x\\\n"}, {"task_id", "0x1"}}, 1.5);
  out.single("vxworks_uptime_seconds", "Uptime.", MetricType::Gauge, 10.25);
  const ExpositionCheck check = checkExposition(sink.text());
  ASSERT_TRUE(check.ok) << check.error << " (line " << check.line << ")";
  ASSERT_EQ(check.samples.size(), 2U);
  EXPECT_EQ(check.samples[0].labels.at("task"), "t\"x\\\n");
  EXPECT_DOUBLE_EQ(check.samples[0].value, 1.5);
}

}  // namespace
}  // namespace vxexp
