#!/usr/bin/env bash
# Current Homebrew no longer bootstraps Intel macOS. Retain an official,
# mutually dated brew/core snapshot, with Intel bottles, in a separate prefix.
set -euo pipefail
[[ $(uname -m) == x86_64 ]]
export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_FROM_API=1
brew_commit=c2439e92e0fe7b7a4faab840732de259e713ffbd
core_commit=71edbc9f6d9fdc47d5af8ceb10a9c8fb4bebbebe
sudo install -d -o "$(id -un)" \
  /usr/local/Homebrew /usr/local/Cellar /usr/local/Caskroom \
  /usr/local/bin /usr/local/etc /usr/local/Frameworks /usr/local/include \
  /usr/local/lib /usr/local/opt /usr/local/sbin /usr/local/share \
  /usr/local/var/homebrew/linked
checkout() {
  local dir=$1 repo=$2 commit=$3
  if [[ ! -d "$dir/.git" ]]; then
    git init "$dir"
    git -C "$dir" remote add origin "https://github.com/$repo.git"
  fi
  git -C "$dir" fetch --depth 1 origin "$commit"
  git -C "$dir" checkout --detach "$commit"
  [[ $(git -C "$dir" rev-parse HEAD) == "$commit" ]]
}
checkout /usr/local/Homebrew Homebrew/brew "$brew_commit"
ln -sf ../Homebrew/bin/brew /usr/local/bin/brew
checkout /usr/local/Homebrew/Library/Taps/homebrew/homebrew-core \
  Homebrew/homebrew-core "$core_commit"
/usr/local/bin/brew --version
