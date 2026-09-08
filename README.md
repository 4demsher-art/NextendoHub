# NextendoHub-iOS

A native iOS shell that runs the NextendoHub web UI in a `WKWebView`. It is the
iOS equivalent of the Electron `NextendoHub.exe` — same renderer, native bridge
instead of Electron's preload.

**You need a Mac with Xcode to turn this into an `.ipa`.** See `PLAN.md` for the
full write-up (including what the original `NextendoApp.ipa` was and why an ipa
cannot be produced on Windows).

## Quick start (macOS)

```bash
brew install xcodegen          # one-time
cd NextendoHub-iOS

# open in Xcode, set Team + bundle id, run on a device/simulator:
xcodegen generate && open NextendoHub.xcodeproj

# …or straight to an .ipa (edit ExportOptions.plist teamID + project.yml first):
./build-ipa.sh                 # -> build/NextendoHub.ipa
```

## What maps to what

| Electron | iOS |
|---|---|
| `renderer/index.html` | `www/index.html` (unchanged) |
| `preload.js` + `contextBridge` | `www/bridge-shim.js` (defines `window.nextendo`) |
| `main.js` IPC handlers | `Sources/NextendoBridge.swift` |
| `safeStorage` session.bin | `Sources/Keychain.swift` (per-account tokens) |
| `dialog.showSaveDialog` (save download) | iOS share sheet (`UIActivityViewController`) |
| tray / "close to tray" | n/a (app just backgrounds) |
| "launch at startup" | n/a — bridge returns `{ok:false}`; the Settings toggle reverts |

## Before you ship

- Add `icon-1024.png` you're happy with (the one here is regenerated from the
  original's colours, not the real artwork). Xcode 15 can fill the other sizes
  from a single 1024 if you switch the asset to "Single Size".
- Set `DEVELOPMENT_TEAM` / `PRODUCT_BUNDLE_IDENTIFIER` in `project.yml` and
  `teamID` / `method` in `ExportOptions.plist`.
- For a sideload build, uncomment `get-task-allow` in `NextendoHub.entitlements`
  (matches the original ipa).
- If `window.nextendo` is undefined at runtime, the page CSP blocked the injected
  user script — it's also loaded as `<script src="bridge-shim.js">` in
  `index.html`, so make sure that file ships in `www/`.
