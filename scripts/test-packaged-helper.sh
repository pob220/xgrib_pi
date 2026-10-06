#!/bin/sh
set -eu

stage=${1:?usage: test-packaged-helper.sh STAGING_PREFIX}
plugin_root="$stage/share/opencpn/plugins/xgrib_pi"
helper="$plugin_root/bin/environmental-grib"

test -x "$helper"
test -x "$plugin_root/libexec/environmental-grib.bin"
test -d "$plugin_root/runtime/lib"
test -f "$plugin_root/runtime/share/eccodes/definitions/grib1/boot.def"
test -f "$plugin_root/runtime/share/eccodes/samples/regular_ll_sfc_grib1.tmpl"
test -f "$plugin_root/runtime/share/proj/proj.db"

tmp=${TMPDIR:-/tmp}/xgrib-package-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$helper" capabilities >"$tmp/capabilities.json"
jq -e '.schemaVersion == 1 and .operations == ["generateEnvironment"] and .antimeridianBoundingBoxes == true' \
  "$tmp/capabilities.json" >/dev/null

cat >"$tmp/job.json" <<EOF
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
  >"$tmp/progress.jsonl"
jq -e '.status == "complete" and .schemaVersion == 1' \
  "$tmp/result.json" >/dev/null
grep -q '"event":"started"' "$tmp/progress.jsonl"
grep -q '"event":"complete"' "$tmp/progress.jsonl"

# Exercise the actual packaged writer and crop/merge path without a network.
jq --arg output "$tmp/dateline.grb" '
  .request.bbox = {west:170,south:-1,east:-170,north:1} |
  .request.weatherProvider = "none" |
  .request.currentSource = "synthetic" |
  .request.currentGridSpacingDeg = 0.25 |
  .request.output = $output |
  .request.dryRun = false
' "$tmp/job.json" >"$tmp/dateline-job.json"
"$helper" run-job --job "$tmp/dateline-job.json" --result "$tmp/dateline-result.json" \
  >"$tmp/dateline-progress.jsonl"
jq -e '.status == "complete" and .result.message_count == 6' \
  "$tmp/dateline-result.json" >/dev/null
"$helper" inspect-grib "$tmp/dateline.grb" >"$tmp/dateline-inspection.json"
jq -e '.message_count == 6 and all(.messages[]; .grid.west == 170 and .grid.east == 190)' \
  "$tmp/dateline-inspection.json" >/dev/null

printf '%s\n' "Packaged xGRIB helper test passed"
