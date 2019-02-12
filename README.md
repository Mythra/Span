# Span #

| Build Type    | Status                                                                                                                                                           |
|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Ubuntu  16.04 | [![Build Status](https://dev.azure.com/ecoan/ecoan/_apis/build/status/SecurityInsanity.Span)](https://dev.azure.com/ecoan/ecoan/_build/latest?definitionId=2) |
| FreeBSD v11.2 | [![Build Status](https://api.cirrus-ci.com/github/SecurityInsanity/Span.svg?branch=master)](https://cirrus-ci.com/github/SecurityInsanity/Span)                  |

Span is a port of [Mordor](https://github.com/mozy/mordor) a high-performance
IO Library made by mozy. However Span has been updated to the modern era,
specifically span does not require Boost, and instead opts to use platform
features introduced steadily since C++11. Everything from std::shared_ptr, to
std::string_view which is newer in C++17 has made it in. Not only that, but
it includes several performance impacting changes that make it much faster.

## OS Compatability ##

Actively tested, and supported OS's are:

  * Ubuntu 16.04, and greater
  * FreeBSD v11.2

Other Operating Systems that aren't supported yet, but are supported in the
original Mordor are:

  * Mac-OS X/iOS Support: Was dropped due to Mac-OS X removing a core posix header
    that is no longer possible to use in recent versions. A workaround will
    need to be used.
  * Windows Support: Completely dropped to start, but plan on implementing
    again. Originally I didn't have a strong windows machine to test builds
    on, that has since changed so just need to schedule time to do it.

## Dependencies ##

Span Currently links to the following dependencies in it's build process:

  * [Abseil](https://abseil.io/) - Links to the synchronization part of Abseil
    for effecient mutexes/scoped mutex locks.
  * [BoringSSL](https://boringssl.googlesource.com/boringssl/) - Used for
    TLS/SSL Communications for TLS-Streams.
  * [GLOG](https://github.com/google/glog) - for logging.
  * [GFLAGS](https://github.com/gflags/gflags) - dependency of glog.

It should also be noted we bundle: [Slimsig](https://github.com/ilsken/slimsig)
as part of our codebase (e.g. doesn't require linking too). The license for
this code can be found in the third-party-licenses folder.

_Note: although mordor available at: github.com/mozy/mordor does not include a
license file, they do include a license section in the readme stating which
license they use. to be safe we've copied their copyright info from the readme
into an actual New BSD License which is what they claim they use. You should
distribute this to be safe._

## Building ##

Building is done entirely through [Bazel](https://bazel.build/). Simply instal
bazel, and run: `bazel build //span:span` to build the library, it will
download all necessary dependencies, and compile them for you.

***NOTE: You must have a C++17 compatible compiler.***

You can also run tests with the shell script: `./.ci/run-tests.sh` if you have
bazel installed.

Finally you can build examples like so:

  ```
  bazel build //span:span-cat
  # ./bazel-bin/span/span-cat
  ```
