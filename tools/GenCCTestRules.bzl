"""
Generate cc_test rules from given test_files.

This helps wrap all tests for all of our files.
"""
def GenCcTestRules(
  name,
  prefix,
  test_files,
  deps=[],
  exclude_tests=[],
  default_test_size="small",
  small_tests=[],
  medium_tests=[],
  large_tests=[],
  enormous_tests=[],
  resources=[],
  flaky_tests=[],
  tags=[],
  visibility=None,
):
  for test in _get_test_names(test_files):
    if test in exclude_tests:
      continue
    test_size = default_test_size
    if test in small_tests:
      test_size = "small"
    if test in medium_tests:
      test_size = "medium"
    if test in large_tests:
      test_size = "large"
    if test in enormous_tests:
      test_size = "enormous"
    flaky = 0
    if (test in flaky_tests) or ("flaky" in tags):
      flaky = 1

    native.cc_test(
      name = prefix + test,
      size = test_size,
      srcs = [test],
      deps = deps,
      copts = [
        "-std=c++17",
      ],
      linkopts = [
        "-lm",
        "-lpthread"
      ],
      visibility = visibility
    )

def _get_test_names(test_files):
  test_names = []
  for test_file in test_files:
    test_names += [test_file]
  return test_names
