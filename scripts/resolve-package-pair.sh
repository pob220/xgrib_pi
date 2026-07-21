#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 BUILD_DIRECTORY [PACKAGE_ARCHIVE]" >&2
  exit 2
fi

build_dir=$1
test -d "$build_dir" || {
  echo "Package directory does not exist: $build_dir" >&2
  exit 1
}

if [ "$#" -eq 2 ]; then
  case $2 in
    /*) archive=$2 ;;
    *)
      if [ -f "$2" ]; then
        archive=$2
      else
        archive=$build_dir/$2
      fi
      ;;
  esac
else
  # Leave the glob unmatched when no archive exists. Both zero and multiple
  # candidates then fail instead of accepting an arbitrary find(1) result.
  set -- "$build_dir"/xgrib_pi-*.tar.gz
  if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "Expected exactly one xGRIB package archive in $build_dir." >&2
    echo "Remove stale packages or pass the intended archive explicitly." >&2
    find "$build_dir" -maxdepth 1 -type f -name 'xgrib_pi-*.tar.gz' \
      -print >&2
    exit 1
  fi
  archive=$1
fi

test -f "$archive" || {
  echo "Package archive does not exist: $archive" >&2
  exit 1
}
case $archive in
  *.tar.gz) ;;
  *)
    echo "Package archive must end in .tar.gz: $archive" >&2
    exit 1
    ;;
esac

metadata=${archive%.tar.gz}.xml
test -f "$metadata" || {
  echo "Matching package metadata does not exist: $metadata" >&2
  exit 1
}

printf '%s\n%s\n' "$archive" "$metadata"
