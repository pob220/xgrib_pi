#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: validate-before-push.sh [--build-dir DIRECTORY] [--keep-build]

Runs a clean native Release configure, build, CTest suite, deterministic GRIB
merge/reopen validation, staged-helper test and exact catalogue archive check.
The default temporary build is removed on success; --keep-build retains it.
EOF
}

build_dir=""
keep_build=0
while (( $# )); do
  case $1 in
    --build-dir)
      (( $# >= 2 )) || { usage >&2; exit 2; }
      build_dir=$2
      shift 2
      ;;
    --keep-build)
      keep_build=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

readonly repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

generated_build=0
if [[ -z "$build_dir" ]]; then
  build_dir=$(mktemp -d "${TMPDIR:-/tmp}/xgrib-preflight.XXXXXX")
  generated_build=1
else
  case $build_dir in
    /*) ;;
    *) build_dir="$repo/$build_dir" ;;
  esac
  if [[ -e "$build_dir" ]] && [[ -n "$(find "$build_dir" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
    echo "Preflight build directory is not empty: $build_dir" >&2
    echo "Use a new directory; preflight validation deliberately starts clean." >&2
    exit 1
  fi
  mkdir -p "$build_dir"
fi

cleanup() {
  if (( generated_build == 1 && keep_build == 0 )); then
    rm -rf -- "$build_dir"
  fi
}
trap cleanup EXIT HUP INT TERM

readonly artifact_dir="$build_dir/preflight-artifacts"
readonly stage_dir="$build_dir/preflight-stage"
mkdir -p "$artifact_dir/tests" "$stage_dir"

echo "==> Repository and submodule checks"
git diff --check
submodule_status=$(git submodule status --recursive)
printf '%s\n' "$submodule_status"
if printf '%s\n' "$submodule_status" | grep -Eq '^[+-U]'; then
  echo "A submodule is missing, modified, or at the wrong recorded commit." >&2
  exit 1
fi
git submodule foreach --quiet --recursive \
  'test -z "$(git status --porcelain)"' || {
  echo "A submodule worktree contains uncommitted or untracked files." >&2
  exit 1
}
grep -Fxq 'src/XyGribPanel.cpp' po/POTFILES.in
cmake --list-presets=all >/dev/null

case $(uname -s) in
  Linux) preset=linux-release ;;
  Darwin) preset=macos-arm64-release ;;
  *)
    echo "Use ci/validate-before-push-windows.ps1 on Windows." >&2
    exit 2
    ;;
esac

echo "==> Configure with preset $preset"
configure_args=(--preset "$preset" -B "$build_dir")
jasper_source=${XGRIB_PREFLIGHT_JASPER_SOURCE:-}
if [[ -z "$jasper_source" ]] &&
   [[ -f "$repo/build/_deps/xgrib_jasper-src/CMakeLists.txt" ]]; then
  jasper_source="$repo/build/_deps/xgrib_jasper-src"
fi
if [[ -n "$jasper_source" ]]; then
  [[ -f "$jasper_source/CMakeLists.txt" ]] || {
    echo "Invalid XGRIB_PREFLIGHT_JASPER_SOURCE: $jasper_source" >&2
    exit 1
  }
  echo "Using verified local Jasper source cache: $jasper_source"
  configure_args+=("-DFETCHCONTENT_SOURCE_DIR_XGRIB_JASPER=$jasper_source")
else
  echo "No local Jasper source cache found; CMake will fetch the pinned archive."
fi
cmake "${configure_args[@]}"
echo "==> Build"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
echo "==> CTest"
ctest --test-dir "$build_dir" --output-on-failure \
  --output-junit "$artifact_dir/tests/ctest.xml"
echo "==> Deterministic merge and production-reader reopen"
scripts/run-functional-merge-test.sh "$build_dir" "$artifact_dir"
echo "==> Staged helper"
cmake --install "$build_dir" --prefix "$stage_dir"
scripts/test-packaged-helper.sh "$stage_dir"
echo "==> Package and exact archive validation"
cmake --build "$build_dir" --target package
mapfile -t pair < <(scripts/resolve-package-pair.sh "$build_dir")
(( ${#pair[@]} == 2 ))
scripts/test-catalogue-archive.sh "$build_dir" "${pair[0]}"
XGRIB_SOURCE_COMMIT=$(git rev-parse HEAD) \
  scripts/collect-build-artifacts.sh "$build_dir" preflight-native \
    "$artifact_dir" "${pair[0]}"

echo "Preflight validation passed."
echo "Build directory: $build_dir"
echo "Package: ${pair[0]}"
if (( generated_build == 1 && keep_build == 0 )); then
  echo "Temporary build will now be removed (use --keep-build to retain it)."
fi
