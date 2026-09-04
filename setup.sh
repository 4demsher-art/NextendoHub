#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "==> fetching borealis (legacy)"
rm -rf lib/borealis
mkdir -p lib
git clone --depth 1 --branch legacy https://github.com/natinusala/borealis lib/borealis

echo "==> fetching libretro-common"
rm -rf lib/borealis/library/include/libretro-common
git clone --depth 1 https://github.com/libretro/libretro-common \
  lib/borealis/library/include/libretro-common

echo "==> staging borealis resources into romfs/"
mkdir -p romfs
if [ -d lib/borealis/resources ]; then
  cp -a lib/borealis/resources/. romfs/
fi

echo "==> verifying dependencies"
test -f lib/borealis/library/include/borealis.hpp
test -f lib/borealis/library/include/libretro-common/features/features_cpu.h
test -f lib/borealis/library/borealis.mk

echo "==> done. now run: make"
