#!/usr/bin/env bash
# Native Apple-Silicon validation build.  The historical filename is retained
# because it is part of the Frontend2 CI interface.
set -euo pipefail
set -x

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
git submodule update --init --recursive

export HOMEBREW_NO_AUTO_UPDATE=1
while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  brew list --versions "$package" >/dev/null 2>&1 || brew install "$package"
done <build-deps/macos-deps

brew_prefix=$(brew --prefix)
wx_prefix=$(brew --prefix wxwidgets@3.2)
export PATH="${wx_prefix}/bin:${brew_prefix}/opt/gettext/bin:${brew_prefix}/bin:${PATH}"
export PKG_CONFIG_PATH="${wx_prefix}/lib/pkgconfig:${brew_prefix}/lib/pkgconfig:${brew_prefix}/opt/curl/lib/pkgconfig:${brew_prefix}/opt/openssl@3/lib/pkgconfig"
export CMAKE_PREFIX_PATH="${wx_prefix};${brew_prefix}"
export WX_CONFIG="${wx_prefix}/bin/wx-config-3.2"
export OCPN_TARGET=macos-arm64
export WX_VER=32

build="$repo/build"
stage="$repo/stage"
artifact="$repo/artifacts/macos-arm64"
log_dir="$artifact/logs"
test_dir="$artifact/tests"
package_dir="$artifact/package"
mkdir -p "$build" "$stage" "$log_dir" "$test_dir" "$package_dir"
while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  brew list --versions "$package"
done <build-deps/macos-deps >"$log_dir/dependencies.log"

cmake -S . -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_CONFIG" \
  -DBUNDLE_GENERATOR_RUNTIME=ON \
  2>&1 | tee "$log_dir/configure.log"
cmake --build "$build" --parallel 3 2>&1 | tee "$log_dir/build.log"
ctest --test-dir "$build" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" 2>&1 | tee "$log_dir/test.log"
scripts/run-functional-merge-test.sh "$build" "$artifact" \
  2>&1 | tee "$log_dir/functional-merge.log"
cmake --install "$build" --prefix "$stage" \
  2>&1 | tee "$log_dir/install.log"
cmake --build "$build" --target package \
  2>&1 | tee "$log_dir/package.log"

cp -f "$build"/xgrib_pi-*.tar.gz "$build"/xgrib_pi-*.xml "$package_dir/"
(cd "$package_dir" && find . -maxdepth 1 -type f ! -name checksums.txt \
  -print0 | xargs -0 shasum -a 256 >checksums.txt)
archive=$(find "$package_dir" -name 'xgrib_pi-*.tar.gz' -print -quit)
metadata=$(find "$package_dir" -name 'xgrib_pi-*.xml' -print -quit)
test -n "$archive"
test -n "$metadata"
grep -q '<target>darwin-wx32</target>' "$metadata"
tar -tzf "$archive" >"$test_dir/archive-contents.txt"
grep -q 'libxgrib_pi.dylib' "$test_dir/archive-contents.txt"
grep -q 'environmental-grib' "$test_dir/archive-contents.txt"

jq -n \
  --arg commit "$(git rev-parse HEAD)" \
  --arg os "$(sw_vers -productVersion)" \
  --arg compiler "$(c++ --version | head -1)" \
  --arg cmake "$(cmake --version | head -1)" \
  --arg wx "$("$WX_CONFIG" --version)" \
  --arg eccodes "$(brew list --versions eccodes)" \
  --arg netcdf "$(brew list --versions netcdf)" \
  --arg blosc "$(brew list --versions c-blosc)" \
  --arg proj "$(brew list --versions proj)" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(shasum -a 256 "$archive" | awk '{print $1}')" \
  '{schema: "xgrib-target-result-v1", target: "macos-arm64",
    xgrib_repository_commit: $commit, xgrib_version: "0.1.0.1",
    opencpn_version: "not installed by build job", operating_system: "macOS",
    operating_system_version: $os, architecture: "arm64", compiler: $compiler,
    cmake_version: $cmake, wxwidgets_version: $wx,
    grib_library_versions:
      {eccodes: $eccodes, netcdf: $netcdf, blosc: $blosc, proj: $proj},
    build_status: "passed", test_status: "passed", package_status: "passed",
    metadata_validation_status: "passed", installation_status: "not-run",
    plugin_discovery_status: "not-run", plugin_load_status: "not-run",
    graphical_test_status: "not-run", file_path_display_status: "contract-tested",
    merge_status: "passed", output_validation_status: "passed",
    screenshot_paths: [],
    log_paths: ["logs/dependencies.log", "logs/configure.log",
                "logs/build.log", "logs/test.log",
                "logs/functional-merge.log", "logs/install.log",
                "logs/package.log"],
    package_filename: $package, package_checksum_sha256: $checksum,
    elapsed_time_seconds: null,
    result_classification: "build-and-package-only",
    blocker_or_failure_details:
      "Genuine Apple-Silicon build and deterministic merge ran; OpenCPN GUI runtime was not installed by this job"}' \
  >"$artifact/result.json"
jq '{target, operating_system, operating_system_version, architecture,
     compiler, cmake_version, wxwidgets_version, grib_library_versions}' \
  "$artifact/result.json" >"$artifact/environment.json"
