#!/usr/bin/env bash
# Verify and execute the delivered helper after extraction and relocation.
set -euo pipefail

if [[ $# != 1 || $(uname -s) != Darwin ]]; then
  echo "usage (on macOS): $0 PACKAGE_ARCHIVE" >&2
  exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/xgrib-macos-archive.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
# OpenCPN installs plugins below Application Support, not inside the app that
# CPack stages. A path containing spaces also exercises the installed layout.
mkdir -p "$tmp/Application Support"
tar -xzf "$1" -C "$tmp/Application Support"
helpers=()
while IFS= read -r -d '' file; do
  helpers+=("$file")
done < <(find "$tmp/Application Support" -type f -name environmental-grib -print0)
if [[ ${#helpers[@]} != 1 ]]; then
  echo "Expected exactly one packaged environmental-grib helper" >&2
  exit 1
fi
helper=${helpers[0]}
frameworks="$(dirname "$helper")/../Frameworks"
test -x "$helper"
test -f "$frameworks/libeccodes.dylib"

# Verify every physical library, not just the executable or a --deep traversal
# of this non-app directory. Do not repair the package during validation.
while IFS= read -r -d '' library; do
  codesign --verify --strict --verbose=2 "$library"
done < <(find "$frameworks" -type f -name '*.dylib' -print0)
codesign --verify --strict --verbose=2 "$helper"

# Do not let build-machine DYLD overrides hide broken bundled dependencies.
unset DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FRAMEWORK_PATH
unset DYLD_FALLBACK_FRAMEWORK_PATH DYLD_INSERT_LIBRARIES
cd "$tmp"
"$helper" capabilities >capabilities.json
jq -e '.schemaVersion == 1 and .operations == ["generateEnvironment"]' \
  capabilities.json >/dev/null
cat >job.json <<EOF
{
  "schemaVersion": 1,
  "operation": "generateEnvironment",
  "request": {
    "bbox": {"west": -6.3, "south": 53.0, "east": -4.0, "north": 54.0},
    "start": "2026-07-12T00:00:00Z",
    "hours": 6,
    "stepHours": 3,
    "weatherProvider": "gfs",
    "currentSource": "none",
    "output": "$tmp/output.grb",
    "overwrite": true,
    "dryRun": true
  }
}
EOF
"$helper" run-job --job "$tmp/job.json" --result "$tmp/result.json" \
  >progress.jsonl
jq -e '.status == "complete" and .schemaVersion == 1' result.json >/dev/null
grep -q '"event":"complete"' progress.jsonl
echo "Packaged macOS helper signatures, capabilities and dry-run job passed"
