# Pragmatically define C++ Deps.
def cxx_workspace():
  native.http_archive(
    name = "com_google_absl",
    url = "https://github.com/abseil/abseil-cpp/archive/28080f5f050c9530aa9f2b39c60d8217038d64ff.zip",
    strip_prefix = "abseil-cpp-28080f5f050c9530aa9f2b39c60d8217038d64ff",
    sha256 = "74a398798667f99942907ef324200f39b52e250749f79ca5467a1c711b7e95d0"
  )

  native.http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
  )

  native.http_archive(
    name = "com_googlesource_code_cctz",
    urls = ["https://github.com/google/cctz/archive/master.zip"],
    strip_prefix = "cctz-master",
  )

  native.git_repository(
    name = "boringssl",
    commit = "1c91287e05463520c75877af46b665880d11ab63",
    remote = "https://boringssl.googlesource.com/boringssl",
  )
