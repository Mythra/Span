#!/usr/bin/env bash

set -e

span_files=$( find ./span/ -name \*.hh -or -name \*.cpp | grep -vE "^\\.\\/build\\/" | grep -v "third_party" ) 2>&1
python cpplint.py --headers="h,hh" --counting=detailed $span_files
