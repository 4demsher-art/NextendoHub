#!/usr/bin/env bash
# One-time: pull the borealis GUI library + its runtime resources.
# Run this once (needs git + network), then `make`.
set -euo pipefail
cd "$(dirname "$0")"

BOREALIS_REPO="https://github.com/natinusala/borealis"
# classic borealis (plain-Makefile friendly, brls::TabFrame / brls::List API).
BOREALIS_REF="legacy"          # Borealis legacy branch; compatible with the classic Makefile/API used by this project

if [ ! -d lib/borealis/library ]; then
  echo "==> fetching borealis ($BOREALIS_REF)"
  rm -rf lib/borealis
  git clone "$BOREALIS_REPO" lib/borealis
  git -C lib/borealis checkout "$BOREALIS_REF"
  git -C lib/borealis submodule update --init --recursive
fi

echo "==> staging borealis resources into romfs/"
mkdir -p romfs
cp -r lib/borealis/resources/* romfs/ 2>/dev/null || true
# our own extras alongside them:
#   romfs/cacert.pem   (TLS CA bundle — already in the zip)
#   romfs/icon.jpg      (copied from ./icon.jpg for the in-app header)
cp -f icon.jpg romfs/icon.jpg

echo "==> done.  now run:  make"
