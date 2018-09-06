#!/usr/bin/env bash

output=`bazel query 'kind("cc_.*test", "//span:*")' --noshow_timestamps --noshow_progress --noannounce_rc --noexperimental_ui --color no --logging 0`
if [[ "$output" =~ .*\.\.\..* ]]; then
    output=`echo "$output" | awk '{if(NR>1)print}'`
fi
output=`echo "$output" | tr '\n' ' '`
bazel test $output --test_verbose_timeout_warnings --linkopt=-lm --linkopt=-pthread --test_output=errors
