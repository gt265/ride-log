#!/usr/bin/env bash
# Checks the simulator's JavaScript port of ride_logic.h gives exactly the same results as the C++.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wno-unused-result trace_runner.cpp -o /tmp/ridelog_trace_runner
node make_trace.js > /tmp/ridelog_trace.txt
/tmp/ridelog_trace_runner < /tmp/ridelog_trace.txt > /tmp/ridelog_cpp.out
node trace_runner.js < /tmp/ridelog_trace.txt > /tmp/ridelog_js.out
diff /tmp/ridelog_cpp.out /tmp/ridelog_js.out && echo "C++ and JavaScript give identical results"
