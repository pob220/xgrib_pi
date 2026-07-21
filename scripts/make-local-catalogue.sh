#!/bin/sh
set -eu

build_dir=${1:?usage: make-local-catalogue.sh BUILD_DIR OUTPUT_DIR [BASE_URL] [PACKAGE_ARCHIVE]}
output_dir=${2:?usage: make-local-catalogue.sh BUILD_DIR OUTPUT_DIR [BASE_URL] [PACKAGE_ARCHIVE]}
base_url=${3:-http://127.0.0.1:8000}
if [ "$#" -gt 4 ]; then
  echo "usage: make-local-catalogue.sh BUILD_DIR OUTPUT_DIR [BASE_URL] [PACKAGE_ARCHIVE]" >&2
  exit 2
fi
if [ "$#" -eq 4 ]; then
  pair=$("$(dirname "$0")/resolve-package-pair.sh" "$build_dir" "$4")
else
  pair=$("$(dirname "$0")/resolve-package-pair.sh" "$build_dir")
fi
archive=$(printf '%s\n' "$pair" | sed -n '1p')
metadata=$(printf '%s\n' "$pair" | sed -n '2p')

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
