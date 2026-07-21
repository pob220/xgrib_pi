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
if [ ! -f "$metadata" ]; then
  # Frontend2 normally uses the archive basename for metadata, but Flatpak
  # deliberately uses a different target-oriented XML filename.  A clean
  # package directory still contains exactly one XML for the same release, so
  # accept that deterministic pairing while rejecting stale/ambiguous XML.
  archive_name=${archive##*/}
  version=$(printf '%s\n' "$archive_name" |
    sed -n 's/^xgrib_pi-\([0-9][0-9.]*\)-.*\.tar\.gz$/\1/p')
  test -n "$version" || {
    echo "Cannot determine xGRIB version from package archive: $archive" >&2
    exit 1
  }
  set -- "$build_dir"/xgrib_pi-"$version"-*.xml
  if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "No unique package metadata exists for: $archive" >&2
    echo "Expected a same-basename XML or exactly one same-version XML in $build_dir." >&2
    find "$build_dir" -maxdepth 1 -type f \
      -name "xgrib_pi-$version-*.xml" \
      -print >&2
    exit 1
  fi
  metadata=$1
fi

printf '%s\n%s\n' "$archive" "$metadata"
