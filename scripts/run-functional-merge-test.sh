#!/usr/bin/env bash
set -euo pipefail

if (( $# != 2 )); then
  echo "usage: $0 BUILD_DIRECTORY ARTIFACT_DIRECTORY" >&2
  exit 2
fi

readonly build_dir="$(cd "$1" && pwd)"
readonly artifact_dir="$2"
readonly fixture_dir="${artifact_dir}/fixtures"
readonly test_dir="${artifact_dir}/tests"
helper="${build_dir}/generator/environmental-grib.bin"
reader="${build_dir}/xgrib_reader_integration_tests"
if [[ ! -x "$helper" ]]; then
  helper="${build_dir}/generator/environmental-grib"
fi
if [[ ! -x "$reader" && -x "${build_dir}/Release/xgrib_reader_integration_tests" ]]; then
  reader="${build_dir}/Release/xgrib_reader_integration_tests"
fi
test -x "$helper"
test -x "$reader"

mkdir -p "${fixture_dir}" "${test_dir}"

sha256_files() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$@"
  else
    shasum -a 256 "$@"
  fi
}

"${build_dir}/generator/environmental_grib_merge_tests" "${fixture_dir}" \
  >"${test_dir}/fixture-test.log" 2>&1

"$helper" merge-environment-gribs \
  --weather "${fixture_dir}/wind-known.grb2" \
  --current "${fixture_dir}/current-differing.grb" \
  --output "${test_dir}/combined-cli.grb2" --overwrite \
  >"${test_dir}/merge-result.json"

jq -e '
  .success == true and
  .output_message_count == 10 and
  .output_inspection.message_count == 10 and
  .output_inspection.short_name_counts["10u"] == 2 and
  .output_inspection.short_name_counts["10v"] == 2 and
  .output_inspection.current_component_counts.u_49 == 3 and
  .output_inspection.current_component_counts.v_50 == 3 and
  .output_inspection.valid_times ==
    ["20260712T0000", "20260712T0300", "20260712T0600"] and
  (.warnings | length) == 1 and
  (.errors | length) == 0
' "${test_dir}/merge-result.json" >/dev/null

"$reader" \
  "${test_dir}/combined-cli.grb2" --combined \
  >"${test_dir}/xgrib-reader-reopen.log" 2>&1

"$helper" inspect-grib \
  "${test_dir}/combined-cli.grb2" \
  >"${test_dir}/combined-inspection.json"

(cd "$artifact_dir" && sha256_files \
  fixtures/wind-known.grb2 \
  fixtures/current-matching.grb \
  fixtures/current-differing.grb \
  fixtures/current-incompatible-area.grb \
  fixtures/current-incompatible-time.grb \
  fixtures/corrupt.grb \
  fixtures/combined-known.grb2 \
  fixtures/combined-matching.grb2 \
  fixtures/wind-only-combined.grb2 \
  fixtures/current-only-combined.grb \
  fixtures/fixture-manifest.json \
  tests/combined-cli.grb2 >tests/checksums.txt)

echo "functional merge validation passed: ${test_dir}/combined-cli.grb2"
