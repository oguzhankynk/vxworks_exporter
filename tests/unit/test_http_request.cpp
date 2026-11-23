#include <gtest/gtest.h>

#include <string>

#include "core/http_request.hpp"

namespace vxexp {
namespace {

Route route(const std::string& request) {
  return routeRequest(request.data(), request.size());
}

bool complete(const std::string& request) {
  return requestHeadComplete(request.data(), request.size());
}

TEST(RouteRequestTest, MetricsPath) {
  EXPECT_EQ(route("GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n"), Route::Metrics);
  EXPECT_EQ(route("GET /metrics HTTP/1.0\r\n\r\n"), Route::Metrics);
  EXPECT_EQ(route("GET /metrics?debug=1 HTTP/1.1\r\n\r\n"), Route::Metrics);
}

TEST(RouteRequestTest, IndexPath) {
  EXPECT_EQ(route("GET / HTTP/1.1\r\n\r\n"), Route::Index);
  EXPECT_EQ(route("GET /?x HTTP/1.1\r\n\r\n"), Route::Index);
}

TEST(RouteRequestTest, OtherPathsAreNotFound) {
  EXPECT_EQ(route("GET /metricsx HTTP/1.1\r\n\r\n"), Route::NotFound);
  EXPECT_EQ(route("GET /metric HTTP/1.1\r\n\r\n"), Route::NotFound);
  EXPECT_EQ(route("GET /favicon.ico HTTP/1.1\r\n\r\n"), Route::NotFound);
}

TEST(RouteRequestTest, OnlyGetIsAllowed) {
  EXPECT_EQ(route("POST /metrics HTTP/1.1\r\n\r\n"), Route::MethodNotAllowed);
  EXPECT_EQ(route("HEAD /metrics HTTP/1.1\r\n\r\n"), Route::MethodNotAllowed);
  EXPECT_EQ(route("GETX /metrics HTTP/1.1\r\n\r\n"), Route::MethodNotAllowed);
}

TEST(RouteRequestTest, AcceptsBareNewlines) {
  EXPECT_EQ(route("GET /metrics HTTP/1.1\n\n"), Route::Metrics);
}

TEST(RouteRequestTest, RejectsMalformedRequestLines) {
  EXPECT_EQ(route(""), Route::BadRequest);
  EXPECT_EQ(route("GET /metrics HTTP/1.1"), Route::BadRequest);  // no line end
  EXPECT_EQ(route("GET\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route(" /metrics HTTP/1.1\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route("GET metrics HTTP/1.1\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route("GET /metrics\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route("GET /metrics HTTP/2.0\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route("GET /metrics HTTP/1.1x\r\n\r\n"), Route::BadRequest);
  EXPECT_EQ(route("GET  /metrics HTTP/1.1\r\n\r\n"), Route::BadRequest);
}

TEST(RequestHeadCompleteTest, NeedsAnEmptyLine) {
  EXPECT_FALSE(complete(""));
  EXPECT_FALSE(complete("GET / HTTP/1.1\r\n"));
  EXPECT_FALSE(complete("GET / HTTP/1.1\r\nHost: x\r\n"));
  EXPECT_TRUE(complete("GET / HTTP/1.1\r\n\r\n"));
  EXPECT_TRUE(complete("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
  EXPECT_TRUE(complete("GET / HTTP/1.1\n\n"));
}

}  // namespace
}  // namespace vxexp
