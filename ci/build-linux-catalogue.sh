#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:-/src}
build_dir=${2:-/work/build}
stage_dir=${3:-/work/stage}
artifact_dir=${4:-/work/artifacts/${OCPN_TARGET:-linux}}
log_dir=${artifact_dir}/logs
test_dir=${artifact_dir}/tests
package_dir=${artifact_dir}/package

mkdir -p "$build_dir" "$stage_dir" "$log_dir" "$test_dir" "$package_dir"

cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUNDLE_GENERATOR_RUNTIME=ON \
  -DXGRIB_USE_BUNDLED_JASPER=ON 2>&1 | tee "$log_dir/configure.log"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
  2>&1 | tee "$log_dir/build.log"
ctest --test-dir "$build_dir" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" 2>&1 | tee "$log_dir/test.log"
"$source_dir/scripts/run-functional-merge-test.sh" "$build_dir" \
  "$artifact_dir" 2>&1 | tee "$log_dir/functional-merge.log"
if ! cmake --install "$build_dir" --prefix "$stage_dir" \
    >"$log_dir/install.log" 2>&1; then
  echo "Staged install failed; see $log_dir/install.log" >&2
  exit 1
fi
echo "Staged install completed; full output is in $log_dir/install.log"
"$source_dir/scripts/test-packaged-helper.sh" "$stage_dir" \
  2>&1 | tee "$log_dir/packaged-helper.log"
cmake --build "$build_dir" --target package \
  2>&1 | tee "$log_dir/package.log"
"$source_dir/scripts/test-catalogue-archive.sh" "$build_dir" \
  2>&1 | tee "$log_dir/archive-validation.log"
"$source_dir/scripts/collect-build-artifacts.sh" "$build_dir" \
  "${OCPN_TARGET:-linux}" "$artifact_dir"
