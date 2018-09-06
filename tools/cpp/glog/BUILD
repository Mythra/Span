licenses(["notice"])

package(default_visibility = ["//visibility:public"])

pkg_name = "glog"
pkg_version = "0.3.5"

package_file = pkg_name + "-" + pkg_version + ".tar.gz"
package_dir = pkg_name + "-" + pkg_version

include_files = [
    "include/glog/log_severity.h",
    "include/glog/logging.h",
    "include/glog/raw_logging.h",
    "include/glog/stl_logging.h",
    "include/glog/vlog_is_on.h",
]

lib_files = [
    "lib/libglog.a",
]

common_script = [
    'export UNWIND_DIR=$$(pwd)/$(GENDIR)/tools/cpp/libunwind/',
    'export INSTALL_DIR=$$(pwd)/$(@D)',
    'export TMP_DIR=$$(mktemp -d -t glog.XXXXX)',
    'mkdir -p $$TMP_DIR',
    'cp -R $(SRCS) $$TMP_DIR',
    'cd $$TMP_DIR',
    'tar xfz ' + package_file,
    'cd ' + package_dir,
]

mac_script = "\n".join(common_script + [
    './configure --prefix=$$INSTALL_DIR --enable-shared=no',
    'make install',
    'rm -rf $$TMP_DIR',
])

linux_script = "\n".join(common_script + [
     'export VAR_LIBS="-Wl,--rpath -Wl,$$UNWIND_DIR/lib -L$$UNWIND_DIR/lib"',
     'export VAR_INCL="-I$$UNWIND_DIR/include"',
     '[[ `uname` == "FreeBSD" ]] && export VAR_LD="-L$$UNWIND_DIR/lib -lexecinfo" || export VAR_LD="-L$$UNWIND_DIR/lib"',
     "export FIRST_PASS_CMAKE=$$(awk -v 'n=385' -v 's=if(Backtrace_FOUND)\\n  target_link_libraries (glog $${Backtrace_LIBRARIES})\\nendif()\\n' '(NR==n) { print s } 1' CMakeLists.txt)",
     "echo \"$$FIRST_PASS_CMAKE\" > CMakeLists.txt",
     "export SECOND_PASS_CMAKE=$$(awk -v 'n=59' -v 's=find_package(Backtrace)\\n' '(NR==n) { print s } 1' CMakeLists.txt)",
     "echo \"$$SECOND_PASS_CMAKE\" > CMakeLists.txt",
     './configure --prefix=$$INSTALL_DIR --enable-shared=no LIBS="$$VAR_LIBS" CPPFLAGS="$$VAR_INCL" LDFLAGS="$$VAR_LD"',
     'make install LIBS="$$VAR_LIBS" CPPFLAGS="$$VAR_INCL" LDFLAGS="$$VAR_LD"',
     'rm -rf $$TMP_DIR',
])

genrule(
    name = "glog-srcs",
    srcs = select({
        "//tools/platform:darwin": [package_file],
        "//conditions:default": [
            package_file,
            "//tools/cpp/libunwind:libunwind-files",
         ]
    }),
    outs = include_files + lib_files,
    cmd = select({
        "//tools/platform:darwin": mac_script,
        "//conditions:default": linux_script,
    }),
)

cc_library(
    name = "glog-cxx",
    srcs = ["empty.cc"] + lib_files,
    deps = [
      "//tools/cpp/gflags:gflags-cxx",
    ],
    hdrs = include_files,
    includes = [
        "include",
    ],
    linkstatic = 1,
)

filegroup(
    name = "glog",
    srcs = [
        ":glog-cxx",
    ]
)

filegroup(
    name = "glog-files",
    srcs = include_files + lib_files
)

