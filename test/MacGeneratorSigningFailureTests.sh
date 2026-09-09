#!/usr/bin/env bash
# Portable failure/argument checks; native signature integrity is tested on Mac.
set -euo pipefail
source_dir=$1
cmake_bin=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/xgrib-signing.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/with spaces/bin" "$tmp/with spaces/Frameworks"
helper="$tmp/with spaces/bin/environmental-grib"
library="$tmp/with spaces/Frameworks/libtest.1.dylib"
touch "$helper" "$library"
ln -s libtest.1.dylib "$tmp/with spaces/Frameworks/libtest.dylib"
export XGRIB_SIGNING_TEST_LOG="$tmp/commands"
cat >"$tmp/codesign" <<'EOF'
#!/usr/bin/env bash
set -eu
file=${!#}
printf '%s %s\n' "$1" "$(basename "$file")" >>"$XGRIB_SIGNING_TEST_LOG"
test -f "$file"
if [[ "$1" == "${XGRIB_SIGNING_TEST_FAIL:-}" ]]; then
  echo "injected codesign failure" >&2
  exit 1
fi
EOF
chmod +x "$tmp/codesign"
cat >"$tmp/run.cmake" <<'EOF'
include("${SOURCE_DIR}/cmake/SignMacGeneratorRuntime.cmake")
xgrib_sign_mac_generator_runtime("${HELPER}")
EOF
run_signing() {
  "$cmake_bin" "-DSOURCE_DIR=$source_dir" "-DHELPER=$helper" \
    "-D_xgrib_codesign=$tmp/codesign" -P "$tmp/run.cmake" >"$tmp/result" 2>&1
}
run_signing
cat >"$tmp/expected" <<'EOF'
--force libtest.1.dylib
--force environmental-grib
--verify libtest.1.dylib
--verify environmental-grib
EOF
diff -u "$tmp/expected" "$tmp/commands"
test -L "$tmp/with spaces/Frameworks/libtest.dylib"

export XGRIB_SIGNING_TEST_FAIL=--force
if run_signing; then
  echo "Signing failure was ignored" >&2
  exit 1
fi
grep -q 'Cannot sign' "$tmp/result"
export XGRIB_SIGNING_TEST_FAIL=--verify
if run_signing; then
  echo "Verification failure was ignored" >&2
  exit 1
fi
grep -q 'Invalid signature' "$tmp/result"
unset XGRIB_SIGNING_TEST_FAIL
rm "$helper"
if run_signing; then
  echo "Missing helper was ignored" >&2
  exit 1
fi
grep -q 'helper not found' "$tmp/result"
echo "Signing failure handling, paths with spaces and symlink checks passed"
