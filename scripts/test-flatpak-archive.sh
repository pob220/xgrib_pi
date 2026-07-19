#!/bin/sh
set -eu

build_dir=${1:?usage: test-flatpak-archive.sh BUILD_DIRECTORY}
archive=$(find "$build_dir" -maxdepth 1 -type f \
  -name 'xgrib_pi-*flatpak*.tar.gz' -print -quit)
metadata=$(find "$build_dir" -maxdepth 1 -type f \
  -name 'xgrib_pi-*flatpak*.xml' -print -quit)
test -n "$archive"
test -n "$metadata"
grep -q '<name> xGRIB </name>' "$metadata"
grep -q '<target>flatpak-' "$metadata"
grep -q '<target-version>' "$metadata"
grep -q '<tarball-url>' "$metadata"

tmp=${TMPDIR:-/tmp}/xgrib-flatpak-archive-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
tar -xzf "$archive" -C "$tmp"
top_count=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | wc -l)
test "$top_count" -eq 1
root=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d -print -quit)
test -n "$root"
case $(basename "$root") in
  xgrib_pi-flatpak-*) ;;
  *) exit 1 ;;
esac

test -f "$root/lib/opencpn/libxgrib_pi.so"
test -x "$root/share/opencpn/plugins/xgrib_pi/bin/environmental-grib"
test -x "$root/share/opencpn/plugins/xgrib_pi/libexec/environmental-grib.bin"
test -f "$root/share/opencpn/plugins/xgrib_pi/runtime/licenses/jasper/LICENSE.txt"
test ! -e "$root/include"
test ! -e "$root/bin"
test ! -e "$root/cmake"
test ! -e "$root/lib/pkgconfig"
test ! -e "$root/lib/cmake"
test ! -e "$root/share/proj"
test -z "$(find "$root/lib" -maxdepth 1 -type f \
  \( -name '*.a' -o -name '*.la' -o -name 'lib*.so*' \) -print -quit)"
"$(dirname "$0")/test-packaged-helper.sh" "$root"

printf '%s\n' "Flatpak archive test passed: $(basename "$archive")"
