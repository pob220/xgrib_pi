#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: test-catalogue-archive.sh BUILD_DIRECTORY [PACKAGE_ARCHIVE]" >&2
  exit 2
fi
build_dir=$1
pair=$("$(dirname "$0")/resolve-package-pair.sh" "$@")
archive=$(printf '%s\n' "$pair" | sed -n '1p')
metadata=$(printf '%s\n' "$pair" | sed -n '2p')
grep -q '<name> xGRIB </name>' "$metadata"
grep -q '<api-version> 1.21 </api-version>' "$metadata"
grep -q '<source> https://github.com/pob220/xgrib_pi </source>' "$metadata"
grep -q '<tarball-url>' "$metadata"

tmp=${TMPDIR:-/tmp}/xgrib-archive-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
tar -xzf "$archive" -C "$tmp"
top_count=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | wc -l)
test "$top_count" -eq 1
root=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d -print -quit)
test -n "$root"

test -f "$root/lib/opencpn/libxgrib_pi.so"
test -x "$root/share/opencpn/plugins/xgrib_pi/bin/environmental-grib"
test -x "$root/share/opencpn/plugins/xgrib_pi/libexec/environmental-grib.bin"
test -f "$root/share/opencpn/plugins/xgrib_pi/runtime/licenses/jasper/LICENSE.txt"
"$(dirname "$0")/test-packaged-helper.sh" "$root"

printf '%s\n' "Catalogue archive test passed: $(basename "$archive")"
