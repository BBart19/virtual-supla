#!/usr/bin/env bash

cd "$(dirname "$0")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

restore_git_symlinks() {
  git -C src config core.symlinks true

  while IFS= read -r path; do
    [ -z "$path" ] && continue

    full_path="src/$path"
    target="$(git -C src show "HEAD:$path")" || exit 1

    if [ -L "$full_path" ] && [ "$(readlink "$full_path")" = "$target" ]; then
      continue
    fi

    rm -f "$full_path"

    if ! ln -s "$target" "$full_path"; then
      echo -e "${RED}Failed to restore symlink:${NC} $full_path -> $target"
      echo "If you are building from /mnt/c in WSL, move the project to your Linux home directory and run install there."
      exit 1
    fi
  done < <(git -C src ls-files -s | awk '$1 == "120000" { print $4 }')
}

echo "Getting the sources."

for cmd in git make g++; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo -e "${RED}Missing required command: ${cmd}${NC}"
    echo "Install dependencies first, for example:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y git libssl-dev build-essential curl"
    exit 1
  fi
done

if [ ! -d src ]; then
  git clone https://github.com/lukbek/supla-core.git -q --single-branch --branch supla-mqtt-dev src >/dev/null || exit 1
fi

if [ -d src/.git ]; then
  (cd src && git pull >/dev/null && cd ..) || exit 1
  restore_git_symlinks
else
  echo "Using bundled sources from ./src."
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
