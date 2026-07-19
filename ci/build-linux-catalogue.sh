#!/bin/sh
set -eu

source_dir=${1:-/src}
build_dir=${2:-/work/build}
stage_dir=${3:-/work/stage}

cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUNDLE_GENERATOR_RUNTIME=ON \
  -DXGRIB_USE_BUNDLED_JASPER=ON
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$build_dir" --output-on-failure
cmake --install "$build_dir" --prefix "$stage_dir"
"$source_dir/scripts/test-packaged-helper.sh" "$stage_dir"
cmake --build "$build_dir" --target package
"$source_dir/scripts/test-catalogue-archive.sh" "$build_dir"
