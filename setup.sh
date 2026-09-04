#!/usr/bin/env bash
# One-time: pull the borealis GUI library + its runtime resources.
# Run this once (needs git + network), then `make`.
set -euo pipefail
cd "$(dirname "$0")"

BOREALIS_REPO="https://github.com/natinusala/borealis"
# classic borealis (plain-Makefile friendly, brls::TabFrame / brls::List API).
# Pinned to a verified-existing commit on `main` (checked against the GitHub
# API before pinning) so this doesn't depend on a moving branch tip.
BOREALIS_REF="20e2d33b6c4ffce139ce304c503c04f5b94da920"

if [ ! -d lib/borealis/library ]; then
  echo "==> fetching borealis ($BOREALIS_REF)"
  rm -rf lib/borealis
  mkdir -p lib/borealis
  git -C lib/borealis init -q
  git -C lib/borealis remote add origin "$BOREALIS_REPO"
  git -C lib/borealis fetch --depth 1 origin "$BOREALIS_REF"
  git -C lib/borealis checkout -q FETCH_HEAD
  git -C lib/borealis submodule update --init --recursive --depth 1
fi

echo "==> staging borealis resources into romfs/"
mkdir -p romfs
cp -r lib/borealis/resources/* romfs/ 2>/dev/null || true
# our own extras alongside them:
#   romfs/cacert.pem   (TLS CA bundle — already in the zip)
#   romfs/icon.jpg      (copied from ./icon.jpg for the in-app header)
cp -f icon.jpg romfs/icon.jpg

echo "==> done.  now run:  make"
