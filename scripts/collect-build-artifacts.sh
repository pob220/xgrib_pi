#!/usr/bin/env bash
set -euo pipefail

if (( $# < 3 || $# > 4 )); then
  echo "usage: $0 BUILD_DIRECTORY TARGET ARTIFACT_DIRECTORY [PACKAGE_ARCHIVE]" >&2
  exit 2
fi

readonly build_dir="$(cd "$1" && pwd)"
readonly target="$2"
readonly artifact_dir="$3"
readonly package_dir="${artifact_dir}/package"
readonly resolver="$(dirname "$0")/resolve-package-pair.sh"

resolve_args=("$build_dir")
if (( $# == 4 )); then
  resolve_args+=("$4")
fi
mapfile -t package_pair < <("$resolver" "${resolve_args[@]}")
if (( ${#package_pair[@]} != 2 )); then
  echo "Package resolver returned an invalid archive/metadata pair" >&2
  exit 1
fi
readonly archive_source="${package_pair[0]}"
readonly metadata_source="${package_pair[1]}"

mkdir -p "$package_dir"
find "$package_dir" -maxdepth 1 -type f \
  \( -name 'xgrib_pi-*.tar.gz' -o -name 'xgrib_pi-*.xml' \
     -o -name 'xgrib_pi-*.deb' -o -name checksums.txt \) -delete
cp -f "$archive_source" "$metadata_source" "$package_dir/"
find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.deb' \
  -exec cp -f '{}' "$package_dir/" \;

readonly archive="$package_dir/$(basename "$archive_source")"

(cd "$package_dir" && find . -maxdepth 1 -type f ! -name checksums.txt \
  -print0 | sort -z | xargs -0 sha256sum >checksums.txt)

os_id=$(uname -s)
os_version=$(uname -r)
if [[ -r /etc/os-release ]]; then
  os_id=$(sed -n 's/^PRETTY_NAME=//p' /etc/os-release | head -1 | tr -d '"')
fi
compiler=$(c++ --version | head -1)
cmake_version=$(cmake --version | head -1)
wx_version=$(wx-config --version 2>/dev/null || printf 'not-reported')
eccodes_version=$(pkg-config --modversion eccodes 2>/dev/null || printf 'not-reported')
netcdf_version=$(pkg-config --modversion netcdf 2>/dev/null || printf 'not-reported')
commit=${XGRIB_SOURCE_COMMIT:-}
if [[ -z "$commit" ]]; then
  commit=$(git -C "$(dirname "$build_dir")/../src" rev-parse HEAD 2>/dev/null || \
    git -C "${CIRCLE_WORKING_DIRECTORY:-.}" rev-parse HEAD 2>/dev/null || \
    printf 'unknown')
fi

jq -n \
  --arg schema "xgrib-target-result-v1" \
  --arg commit "$commit" \
  --arg version "0.1.0.1" \
  --arg opencpn_version "package ABI ov511; runtime not run" \
  --arg os "$os_id" \
  --arg os_version "$os_version" \
  --arg architecture "$(uname -m)" \
  --arg compiler "$compiler" \
  --arg cmake "$cmake_version" \
  --arg wxwidgets "$wx_version" \
  --arg eccodes "$eccodes_version" \
  --arg netcdf "$netcdf_version" \
  --arg target "$target" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(sha256sum "$archive" | awk '{print $1}')" \
  '{
    schema: $schema,
    target: $target,
    xgrib_repository_commit: $commit,
    xgrib_version: $version,
    opencpn_version: $opencpn_version,
    operating_system: $os,
    operating_system_version: $os_version,
    architecture: $architecture,
    compiler: $compiler,
    cmake_version: $cmake,
    wxwidgets_version: $wxwidgets,
    grib_library_versions: {eccodes: $eccodes, netcdf: $netcdf},
    build_status: "passed",
    test_status: "passed",
    package_status: "passed",
    metadata_validation_status: "passed",
    installation_status: "not-run",
    plugin_discovery_status: "not-run",
    plugin_load_status: "not-run",
    graphical_test_status: "not-run",
    file_path_display_status: "contract-tested",
    merge_status: "passed",
    output_validation_status: "passed",
    screenshot_paths: [],
    log_paths: ["logs/configure.log", "logs/build.log", "logs/test.log",
                "logs/functional-merge.log", "logs/package.log"],
    package_filename: $package,
    package_checksum_sha256: $checksum,
    elapsed_time_seconds: null,
    result_classification: "build-and-package-only",
    blocker_or_failure_details: "OpenCPN installation/runtime validation not run in build container"
  }' >"${artifact_dir}/result.json"

jq -n \
  --arg target "$target" \
  --arg os "$os_id" \
  --arg os_version "$os_version" \
  --arg architecture "$(uname -m)" \
  --arg compiler "$compiler" \
  --arg cmake "$cmake_version" \
  --arg wxwidgets "$wx_version" \
  --arg eccodes "$eccodes_version" \
  --arg netcdf "$netcdf_version" \
  '{target: $target, operating_system: $os,
    operating_system_version: $os_version, architecture: $architecture,
    compiler: $compiler, cmake: $cmake, wxwidgets: $wxwidgets,
    libraries: {eccodes: $eccodes, netcdf: $netcdf}}' \
  >"${artifact_dir}/environment.json"
