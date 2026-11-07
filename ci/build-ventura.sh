#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
arch=${XGRIB_MACOS_ARCH:?}
case "$arch" in
  arm64) triplet=arm64-osx-ventura ;;
  x86_64) triplet=x64-osx-ventura ;;
  *) exit 2 ;;
esac
test "$(uname -m)" = "$arch"
export MACOSX_DEPLOYMENT_TARGET=13.0
export WX_VER=32 OCPN_TARGET="macos-$arch"
artifact="$repo/artifacts/ventura-$arch"
mkdir -p "$artifact/logs" "$artifact/tests" "$artifact/package"
exec > >(tee "$artifact/logs/build-ventura.log") 2>&1
set -x

# Homebrew provides build tools and the host's wxWidgets ABI. Every library
# delivered with the generator is built from source by the pinned triplet.
for tool in cmake ninja pkg-config autoconf autoconf-archive automake libtool gettext wxwidgets@3.2 jq; do
  brew list --versions "$tool" >/dev/null 2>&1 || brew install "$tool" </dev/null
done
wx_prefix=$(brew --prefix wxwidgets@3.2)
export PATH="$(brew --prefix gettext)/bin:$PATH"
cache="$HOME/.cache/xgrib-ventura-vcpkg"
mkdir -p "$cache"
export VCPKG_DEFAULT_BINARY_CACHE="$cache"
vcpkg="$repo/build-vcpkg"
git clone https://github.com/microsoft/vcpkg.git "$vcpkg"
git -C "$vcpkg" checkout 319504a5326aa870edde46438c5455fa76305a56
"$vcpkg/bootstrap-vcpkg.sh" -disableMetrics
if ! "$vcpkg/vcpkg" install --triplet "$triplet" \
    --x-manifest-root="$repo/ci/ventura" \
    --x-install-root="$repo/build-ventura-deps" \
    --overlay-triplets="$repo/ci/ventura/triplets" \
    --overlay-ports="$repo/ci/ventura/ports"; then
  find "$vcpkg/buildtrees" -name '*-err.log' | while IFS= read -r log; do
    echo "$log"
    tail -70 "$log"
  done
  exit 1
fi
deps="$repo/build-ventura-deps/$triplet"
export PKG_CONFIG_LIBDIR="$deps/lib/pkgconfig:$deps/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export CMAKE_PREFIX_PATH="$deps;$wx_prefix"
export PROJ_ROOT="$deps"
export WX_CONFIG="$wx_prefix/bin/wx-config-3.2"
if [[ ! -x "$WX_CONFIG" ]]; then export WX_CONFIG="$wx_prefix/bin/wx-config"; fi
sdk=$(xcrun --show-sdk-path)
build="$repo/build"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DXGRIB_MACOS_MINIMUM_VERSION=13.0 \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_CONFIG" \
  -DBZIP2_LIBRARY="$sdk/usr/lib/libbz2.tbd" \
  -DBZIP2_INCLUDE_DIR="$sdk/usr/include" \
  -DZLIB_LIBRARY="$sdk/usr/lib/libz.tbd" \
  -DZLIB_INCLUDE_DIR="$sdk/usr/include" \
  -DECCODES_DEFINITIONS_DIR="$deps/share/eccodes/definitions" \
  -DECCODES_SAMPLES_DIR="$deps/share/eccodes/samples" \
  -DBUNDLE_GENERATOR_RUNTIME=ON
cmake --build "$build" --parallel 3
ctest --test-dir "$build" --output-on-failure \
  --output-junit "$artifact/tests/ctest.xml"
bash scripts/run-functional-merge-test.sh "$build" "$artifact"
cmake --install "$build" --prefix "$repo/stage"
cmake --build "$build" --target package
python3 ci/package-ventura.py "$build" "$artifact/package" "$arch" "$deps"
archive=$(find "$artifact/package" -maxdepth 1 -name '*.tar.gz' -print)
bash scripts/test-macos-catalogue-archive.sh "$archive"
python3 ci/audit-macos-archive.py "$archive" --arch "$arch" --maximum 13.0 \
  >"$artifact/package/macos-$arch-audit.json"
(cd "$artifact/package" && shasum -a 256 *.tar.gz *.xml >SHA256SUMS)
