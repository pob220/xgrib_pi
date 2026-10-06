#!/usr/bin/env bash
# Pre-cache the installed Homebrew formula's source when GNU's primary hosts
# are unreachable. Homebrew still verifies and builds its unchanged formula.
set -euo pipefail

formula=$(brew info --json=v2 gettext)
source_url=$(printf '%s' "$formula" | jq -er '.formulae[0].urls.stable.url')
source_sha=$(printf '%s' "$formula" | jq -er '.formulae[0].urls.stable.checksum')
source_cache=$(brew --cache --build-from-source gettext)
source_name=${source_url##*/}
case "$source_name" in
  gettext-*.tar.gz|gettext-*.tar.xz) ;;
  *) echo "Unexpected gettext source: $source_url" >&2; exit 1 ;;
esac

verified() {
  [[ -f "$1" ]] && printf '%s  %s\n' "$source_sha" "$1" | shasum -a 256 -c -
}
if verified "$source_cache"; then
  exit 0
fi
mkdir -p "$(dirname "$source_cache")"
source_partial="${source_cache}.xgrib-partial"
trap 'rm -f "$source_partial"' EXIT
for mirror in https://mirrors.kernel.org/gnu \
              https://www.mirrorservice.org/sites/ftp.gnu.org/gnu; do
  if curl --fail --silent --show-error --location --connect-timeout 15 \
      --max-time 180 --retry 2 "$mirror/gettext/$source_name" \
      --output "$source_partial" && verified "$source_partial"; then
    mv -f "$source_partial" "$source_cache"
    exit 0
  fi
done
echo 'GNU mirrors unavailable; Homebrew will try its configured source URLs.' >&2
