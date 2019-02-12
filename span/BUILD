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
    "@boringssl//:ssl",
    "@com_google_absl//absl/synchronization",
    "@com_github_gflags_gflags//:gflags",
    "@com_github_glog_glog//:glog"
  ],
  copts = [
    "-std=c++17",
  ],
  linkopts = [
    "-lm",
    "-lpthread"
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
  linkopts = [
    "-lm",
    "-lpthread"
  ],
  deps = [
    ":span",
  ],
)
