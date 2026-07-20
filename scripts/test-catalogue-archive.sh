#!/bin/sh
set -eu

build_dir=${1:?usage: test-catalogue-archive.sh BUILD_DIRECTORY}
archive=$(find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.tar.gz' \
  -print -quit)
metadata=$(find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.xml' \
  -print -quit)

test -n "$archive"
test -n "$metadata"
grep -q '<name> xGRIB </name>' "$metadata"
grep -q '<api-version> 1.21 </api-version>' "$metadata"
grep -q '<source> https://github.com/pob220/xgrib_pi </source>' "$metadata"
grep -q '<tarball-url>' "$metadata"

tmp=${TMPDIR:-/tmp}/xgrib-archive-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
tar -xzf "$archive" -C "$tmp"
root=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d -print -quit)
test -n "$root"

test -f "$root/lib/opencpn/libxgrib_pi.so"
test -x "$root/share/opencpn/plugins/xgrib_pi/bin/environmental-grib"
test -x "$root/share/opencpn/plugins/xgrib_pi/libexec/environmental-grib.bin"
test -f "$root/share/opencpn/plugins/xgrib_pi/runtime/licenses/jasper/LICENSE.txt"
"$(dirname "$0")/test-packaged-helper.sh" "$root"

printf '%s\n' "Catalogue archive test passed: $(basename "$archive")"
