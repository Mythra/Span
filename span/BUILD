package(default_visibility = ["//visibility:public"])

load("//tools:GenCCTestRules.bzl", "GenCcTestRules")

cc_library(
  name = "span",
  srcs = glob([
    "src/**/*.cpp",
  ]),
  hdrs = glob([
    "src/**/*.hh",
  ]),
  deps = [
    "@com_google_absl//absl/synchronization",
    "@boringssl//:ssl",
    "//tools/cpp/glog:glog-cxx",
    "//tools/cpp/gflags:gflags-cxx",
    "//tools/cpp/libunwind:libunwind-cxx",
  ],
  copts = [
    "-std=c++17",
  ],
  strip_include_prefix = "src/",
)

GenCcTestRules(
  name = "cc-tests",
  prefix = "span-",
  test_files = glob([
    "tests/**/*.cpp",
  ]),
  deps = [
    ":span",
    "@com_google_googletest//:gtest_main"
  ],
)

cc_binary(
  name = "span-cat",
  srcs = glob([
    "examples/cat/**/*.cpp",
  ]),
  copts = [
    "-std=c++17",
  ],
  deps = [
    ":span",
  ],
  linkopts = [
    "-lpthread",
  ],
)
