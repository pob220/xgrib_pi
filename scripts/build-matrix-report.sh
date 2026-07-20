#!/usr/bin/env bash
set -euo pipefail

artifact_root=${1:-artifacts}
output_json=${2:-${artifact_root}/matrix.json}
output_markdown=${3:-${artifact_root}/matrix.md}

mapfile -d '' manifests < <(find "$artifact_root" -mindepth 2 -maxdepth 2 \
  -type f -name result.json -print0 | sort -z)
if (( ${#manifests[@]} == 0 )); then
  echo "No result manifests found below $artifact_root" >&2
  exit 1
fi

jq -s '{schema: "xgrib-matrix-result-v1", generated_utc:
          (now | strftime("%Y-%m-%dT%H:%M:%SZ")), targets: sort_by(.target)}' \
  "${manifests[@]}" >"$output_json"

{
  echo '# xGRIB validation matrix'
  echo
  echo '| Target | Classification | Build | Package | Runtime/UI | Merge |'
  echo '| --- | --- | --- | --- | --- | --- |'
  jq -r '.targets[] | "| \(.target) | \(.result_classification) | " +
    "\(.build_status) | \(.package_status) | " +
    "\(.plugin_load_status)/\(.graphical_test_status) | \(.merge_status) |"' \
    "$output_json"
} >"$output_markdown"
