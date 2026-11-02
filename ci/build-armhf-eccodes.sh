#!/bin/sh
# Debian 13 has no 32-bit ecCodes package. Build the same 32-bit-capable
# ecCodes version shipped by Debian 12, without mixing distribution packages.
set -eu
[ "$(dpkg --print-architecture)" = armhf ]
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"
curl --fail --location --retry 3 \
  https://codeload.github.com/ecmwf/ecbuild/tar.gz/refs/tags/3.7.2 -o ecbuild.tar.gz
curl --fail --location --retry 3 \
  https://codeload.github.com/ecmwf/eccodes/tar.gz/refs/tags/2.28.0 -o eccodes.tar.gz
printf '%s  %s\n' \
  7a2d192cef1e53dc5431a688b2e316251b017d25808190faed485903594a3fb9 ecbuild.tar.gz \
  774fb41148ec526a33148a1474ebe293fa8609042af8b3c67681c26c3e3160fb eccodes.tar.gz \
  | sha256sum --check
tar -xzf ecbuild.tar.gz
tar -xzf eccodes.tar.gz
cmake -S eccodes-2.28.0 -B build \
  -Decbuild_DIR="$work/ecbuild-3.7.2/cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DENABLE_FORTRAN=OFF -DENABLE_PYTHON=OFF -DENABLE_NETCDF=OFF \
  -DENABLE_JPG_LIBJASPER=OFF -DENABLE_JPG_LIBOPENJPEG=ON \
  -DENABLE_PNG=ON -DENABLE_AEC=ON -DENABLE_MEMFS=OFF \
  -DENABLE_BUILD_TOOLS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_TESTS=OFF
cmake --build build --parallel "$(nproc)"
cmake --install build
mkdir -p /usr/local/share/licenses/eccodes
cp eccodes-2.28.0/LICENSE eccodes-2.28.0/NOTICE /usr/local/share/licenses/eccodes/
ldconfig
