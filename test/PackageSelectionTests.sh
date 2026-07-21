#!/bin/sh
set -eu

resolver=${1:?usage: PackageSelectionTests.sh RESOLVER}
tmp=${TMPDIR:-/tmp}/xgrib-package-selection-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

touch "$tmp/xgrib_pi-1.0-linux.tar.gz"
touch "$tmp/xgrib_pi-1.0-linux.xml"
pair=$($resolver "$tmp")
test "$(printf '%s\n' "$pair" | sed -n '1p')" = \
  "$tmp/xgrib_pi-1.0-linux.tar.gz"
test "$(printf '%s\n' "$pair" | sed -n '2p')" = \
  "$tmp/xgrib_pi-1.0-linux.xml"

touch "$tmp/xgrib_pi-0.9-linux.tar.gz"
touch "$tmp/xgrib_pi-0.9-linux.xml"
if $resolver "$tmp" >/dev/null 2>&1; then
  echo "Resolver accepted an ambiguous package directory" >&2
  exit 1
fi

$resolver "$tmp" "$tmp/xgrib_pi-1.0-linux.tar.gz" >/dev/null
rm "$tmp/xgrib_pi-1.0-linux.xml"
if $resolver "$tmp" "$tmp/xgrib_pi-1.0-linux.tar.gz" >/dev/null 2>&1; then
  echo "Resolver accepted an archive without matching metadata" >&2
  exit 1
fi

rm "$tmp/xgrib_pi-1.0-linux.tar.gz"
rm "$tmp/xgrib_pi-0.9-linux.tar.gz" "$tmp/xgrib_pi-0.9-linux.xml"
touch "$tmp/xgrib_pi-1.0-flatpak-aarch64.tar.gz"
touch "$tmp/xgrib_pi-1.0-flatpak-aarch64-target.xml"
pair=$($resolver "$tmp")
test "$(printf '%s\n' "$pair" | sed -n '1p')" = \
  "$tmp/xgrib_pi-1.0-flatpak-aarch64.tar.gz"
test "$(printf '%s\n' "$pair" | sed -n '2p')" = \
  "$tmp/xgrib_pi-1.0-flatpak-aarch64-target.xml"

touch "$tmp/xgrib_pi-1.0-flatpak-stale.xml"
if $resolver "$tmp" >/dev/null 2>&1; then
  echo "Resolver accepted ambiguous non-basename package metadata" >&2
  exit 1
fi
