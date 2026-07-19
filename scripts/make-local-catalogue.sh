#!/bin/sh
set -eu

build_dir=${1:?usage: make-local-catalogue.sh BUILD_DIR OUTPUT_DIR [BASE_URL]}
output_dir=${2:?usage: make-local-catalogue.sh BUILD_DIR OUTPUT_DIR [BASE_URL]}
base_url=${3:-http://127.0.0.1:8000}

archive=$(find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.tar.gz' \
  -print -quit)
metadata=$(find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.xml' \
  -print -quit)
test -n "$archive"
test -n "$metadata"

mkdir -p "$output_dir"
archive_name=${archive##*/}
cp "$archive" "$output_dir/$archive_name"

rewritten="$output_dir/plugin.xml"
awk -v url="${base_url%/}/$archive_name" '
  /<\?xml/ { next }
  /<tarball-url>/ {
    print
    print "    " url
    replacing = 1
    next
  }
  replacing && /<\/tarball-url>/ {
    replacing = 0
    print
    next
  }
  !replacing { print }
' "$metadata" > "$rewritten"

{
  printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<plugins>'
  cat "$rewritten"
  printf '%s\n' '</plugins>'
} > "$output_dir/ocpn-plugins.xml"

printf '%s\n' \
  "Local catalogue: $output_dir/ocpn-plugins.xml" \
  "Serve with: python3 -m http.server 8000 --directory $output_dir" \
  "OpenCPN custom catalogue URL: ${base_url%/}/ocpn-plugins.xml"
