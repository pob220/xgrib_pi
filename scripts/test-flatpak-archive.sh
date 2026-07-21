#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: test-flatpak-archive.sh BUILD_DIRECTORY [PACKAGE_ARCHIVE]" >&2
  exit 2
fi
build_dir=$1
pair=$("$(dirname "$0")/resolve-package-pair.sh" "$@")
archive=$(printf '%s\n' "$pair" | sed -n '1p')
metadata=$(printf '%s\n' "$pair" | sed -n '2p')
case $(basename "$archive") in
  *flatpak*.tar.gz) ;;
  *)
    echo "Not an xGRIB Flatpak archive: $archive" >&2
    exit 1
    ;;
esac
grep -q '<name> xGRIB </name>' "$metadata"
target=$(sed -n \
  's:.*<target>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target>.*:\1:p' \
  "$metadata")
case "$target" in
  flatpak-x86_64|flatpak-aarch64) ;;
  *)
    echo "Invalid OpenCPN Flatpak catalogue target: $target" >&2
    exit 1
    ;;
esac
grep -q '<target-version>' "$metadata"
grep -q '<source> https://github.com/pob220/xgrib_pi </source>' "$metadata"
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
