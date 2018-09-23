# Pragmatically define C++ Deps.
def cxx_workspace():
  native.http_archive(
    name         = "com_google_absl",
    strip_prefix = "abseil-cpp-master",
    url          = "https://github.com/abseil/abseil-cpp/archive/master.zip"
  )

  native.http_archive(
     name         = "com_google_googletest",
     strip_prefix = "googletest-master",
     url          = "https://github.com/google/googletest/archive/master.zip"
  )

  native.http_archive(
    name         = "com_github_gflags_gflags",
    strip_prefix = "gflags-master",
    url          = "https://github.com/gflags/gflags/archive/master.zip",
  )

  native.http_archive(
    name         = "com_github_glog_glog",
    strip_prefix = "glog-master",
    url          = "https://github.com/google/glog/archive/master.zip",
  )

  native.http_archive(
    name         = "boringssl",
    url          = "https://boringssl.googlesource.com/boringssl/+archive/master-with-bazel.tar.gz",
    type         = "tar.gz"
  )
