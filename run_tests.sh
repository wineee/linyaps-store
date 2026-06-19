#!/usr/bin/env bash
# run_tests.sh — 编译并运行所有测试
set -euo pipefail

cd "$(dirname "$0")"

echo "=== Configuring ==="
cmake -B build -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "=== Building tests ==="
cmake --build build --target test_backend test_util test_filter

echo ""
echo "=== Running tests ==="
ctest --test-dir build --output-on-failure

echo ""
echo "Done."
