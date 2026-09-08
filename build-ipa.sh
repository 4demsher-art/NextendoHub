#!/usr/bin/env bash
# One-shot: XcodeGen -> archive -> export .ipa.  Run on macOS with Xcode installed.
set -euo pipefail
cd "$(dirname "$0")"

SCHEME=NextendoHub
PROJ=NextendoHub.xcodeproj
OUT=build

command -v xcodegen >/dev/null || { echo "install xcodegen:  brew install xcodegen"; exit 1; }

echo "==> generating $PROJ"
xcodegen generate

rm -rf "$OUT"
mkdir -p "$OUT"

echo "==> archiving"
xcodebuild -project "$PROJ" -scheme "$SCHEME" \
  -configuration Release -sdk iphoneos \
  -archivePath "$OUT/$SCHEME.xcarchive" \
  archive

echo "==> exporting .ipa"
xcodebuild -exportArchive \
  -archivePath "$OUT/$SCHEME.xcarchive" \
  -exportOptionsPlist ExportOptions.plist \
  -exportPath "$OUT"

echo
echo "==> done:  $OUT/$SCHEME.ipa"
ls -lh "$OUT"/*.ipa
