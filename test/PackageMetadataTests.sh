#!/usr/bin/env bash
set -euo pipefail

embedder=${1:?usage: PackageMetadataTests.sh EMBEDDER}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

mkdir -p "$work/payload/lib/opencpn"
printf 'plugin payload\n' >"$work/payload/lib/opencpn/libxgrib_pi.so"
tar -C "$work/payload" -czf "$work/xgrib_pi-0.3.0.0-test.tar.gz" .
cat >"$work/package.xml" <<'EOF'
<plugin><name>xGRIB</name><version>0.3.0.0</version><target>test</target></plugin>
EOF

python3 "$embedder" "$work/xgrib_pi-0.3.0.0-test.tar.gz" \
  "$work/package.xml" "$work/xgrib_pi-0.3.0.0-import.tar.gz"
test "$(tar -tzf "$work/xgrib_pi-0.3.0.0-import.tar.gz" | grep -Ec '(^|/)metadata\.xml$')" -eq 1
tar -xOf "$work/xgrib_pi-0.3.0.0-import.tar.gz" metadata.xml | cmp -s - "$work/package.xml"
tar -tzf "$work/xgrib_pi-0.3.0.0-import.tar.gz" | grep -q 'lib/opencpn/libxgrib_pi.so'

# Re-embedding replaces, rather than duplicates, an existing metadata member.
python3 "$embedder" "$work/xgrib_pi-0.3.0.0-import.tar.gz" \
  "$work/package.xml" "$work/xgrib_pi-0.3.0.0-reimport.tar.gz"
test "$(tar -tzf "$work/xgrib_pi-0.3.0.0-reimport.tar.gz" | grep -Ec '(^|/)metadata\.xml$')" -eq 1

printf '<plugin><version>9.9.9.9</version></plugin>\n' >"$work/invalid.xml"
if python3 "$embedder" "$work/xgrib_pi-0.3.0.0-test.tar.gz" \
    "$work/invalid.xml" "$work/invalid.tar.gz" >/dev/null 2>&1; then
  echo "Version-mismatched metadata was accepted" >&2
  exit 1
fi
