#!/usr/bin/env bash

cd "$(dirname "$0")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

restore_git_symlinks() {
  git config core.symlinks true

  while IFS= read -r path; do
    [ -z "$path" ] && continue

    full_path="$path"
    target="$(git show "HEAD:$path")" || exit 1

    if [ -L "$full_path" ] && [ "$(readlink "$full_path")" = "$target" ]; then
      continue
    fi

    rm -f "$full_path"
    mkdir -p "$(dirname "$full_path")"

    if ! ln -s "$target" "$full_path"; then
      echo -e "${RED}Failed to restore symlink:${NC} $full_path -> $target"
      echo "If you are building from /mnt/c in WSL, move the project to your Linux home directory and run install there."
      exit 1
    fi
  done < <(git ls-files -s | awk '$1 == "120000" { print $4 }')
}

echo "Preparing bundled sources."

for cmd in git make g++; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo -e "${RED}Missing required command: ${cmd}${NC}"
    echo "Install dependencies first, for example:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y git libssl-dev build-essential curl"
    exit 1
  fi
done

if [ ! -d src/supla-dev ]; then
  echo -e "${RED}Bundled sources are missing.${NC}"
  echo "This repository should contain ./src with the patched supla-core sources."
  echo "Clone the full repository instead of a partial copy."
  exit 1
fi

if git rev-parse --git-dir >/dev/null 2>&1; then
  restore_git_symlinks
fi

echo "Building. Be patient."

(cd src/supla-dev/Release && make all && cd ../../..) || exit 1

if [ ! -f supla-virtual-device ]; then
  ln -s src/supla-dev/Release/supla-virtual-device supla-virtual-device
fi

echo -e "${GREEN}OK!${NC}"
./supla-virtual-device -v

if [ ! -f supla-virtual-device.cfg ]; then
  cp supla-virtual-device.cfg.sample supla-virtual-device.cfg
  echo -e "${YELLOW}Sample configuration has been created for you (${NC}supla-virtual-device.cfg${YELLOW})${NC}"
  echo -e "${YELLOW}Adjust it to your needs before launching.${NC}"
fi
