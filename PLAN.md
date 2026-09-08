# NextendoHub → iOS `.ipa` — plan & build

## 0. Why it can't be built on Windows

An `.ipa` is a zip of `Payload/<App>.app/` containing a **compiled ARM64 Mach-O
executable**, a compiled `Assets.car`, and an **Apple code signature**. Producing
any of those requires **macOS + Xcode** (`xcodebuild`, `actool`, `codesign`) and a
signing identity (free personal team, paid developer, or ad-hoc). There is no
cross-toolchain that does this from Windows.

Electron (Chromium + Node.js) also cannot run on iOS — Apple forbids JIT and
bundling a browser engine. So NextendoHub is not "repackaged"; it is **hosted in
a `WKWebView`** by a thin native app, and the `window.nextendo.*` API that the
Electron preload provided is re-implemented natively.

This is exactly how the original `NextendoApp.ipa` works, minus SwiftUI: a native
shell talking to `nextendo.network` over `URLSession`, storing the token in the
Keychain.

---

## 1. What the original `NextendoApp.ipa` told us

From static inspection of the earlier ipa:

| Field | Original | NextendoHub-iOS uses |
|---|---|---|
| `CFBundleIdentifier` | `network.nextendo.app` | `network.nextendo.hub` (change to your own) |
| `CFBundleName` / display | `NextendoApp` / "Nextendo App" | `NextendoHub` / "Nextendo Hub" |
| `MinimumOSVersion` | 16.0 | **16.0** |
| `UIDeviceFamily` | `[1, 2]` (iPhone + iPad) | `[1, 2]` |
| `UIRequiredDeviceCapabilities` | `[arm64]` | `[arm64]` |
| `UISupportedInterfaceOrientations` | portrait only | all (web UI is responsive) |
| `CFBundleURLTypes` | scheme `nextendo`, `nextendo://oauth/callback` | **not needed** — NextendoHub logs in with email+password to `/api/login`, no OAuth redirect |
| `NSCameraUsageDescription` | present (QR scan) | **not needed** — no QR scanning |
| Frameworks | Foundation, UIKit, SwiftUI, Combine, AVFoundation, AudioToolbox, **AuthenticationServices**, **CryptoKit**, **Security** | Foundation, UIKit, **WebKit**, **Security** (Keychain) |
| Signing | ad-hoc, `get-task-allow=true`, `platform-application=true` → a **sideload build** (AltStore / TrollStore / dev cert) | same options; see §4 |
| Asset catalog | `Assets.car` (compiled) | `Assets.xcassets` compiled by Xcode at build time |
| Icons | `AppIcon60x60@2x` (120), `AppIcon76x76@2x~ipad` (152), CgBI-encoded | reused (decoded) + you add 1024 marketing icon |
| Encryption | `cryptid = 0` (not FairPlay-wrapped) | n/a until App Store |

**Not carried over** (native-app features NextendoHub's desktop build had that iOS
either doesn't allow or provides differently):

- Tray icon / "close to tray" — iOS has no concept; the app just backgrounds.
- "Launch at startup" — not permitted on iOS; the bridge returns `{ok:false}` and
  the toggle in Settings simply reverts (renderer already handles that).
- Native "Save file" dialog for cloud-save download — replaced by the iOS **share
  sheet** (`UIActivityViewController`) so you can send the `.zip` to Files, AirDrop, etc.

---

## 2. Architecture

```
NextendoHubApp (SwiftUI @main)
└── WebViewController (UIViewController)
    └── WKWebView   ── loads  www/index.html  (your renderer, byte-for-byte)
        ├── WKUserScript: bridge-shim.js  (injected at documentStart)
        │     defines window.nextendo.* → postMessage to native, await reply
        └── WKScriptMessageHandler "nx"  → NextendoBridge.swift
              re-implements every IPC handler from the Electron main.js:
              serverStatus, online, authStatus, login, register, logout,
              accountsList/Switch/Remove, friends, addFriend, accept, decline,
              profileGet, profileSave, usernameSet/Check, countryList/Set,
              savesList/Delete/Download, modsFavorites, avatarsList/Image,
              startupGet/Set (no-op on iOS)
        Keychain.swift  → per-account bearer tokens (kSecClassGenericPassword,
                          WhenUnlockedThisDeviceOnly — same as the original app)
```

The renderer never changes. `bridge-shim.js` is a drop-in replacement for
`preload.js` + Electron's `contextBridge`/`ipcRenderer`.

---

## 3. Build it (on a Mac)

Prereqs: macOS, Xcode 15+ (iOS 16 SDK or newer), and one of:

- **XcodeGen** (`brew install xcodegen`) — generates the `.xcodeproj` from `project.yml`, or
- open Xcode and make a blank "App" target yourself and drag the `Sources/`, `www/`, `Resources/` in.

```bash
cd NextendoHub-iOS
xcodegen generate          # produces NextendoHub.xcodeproj
open NextendoHub.xcodeproj  # set your Team + bundle id in Signing & Capabilities, then Run on a device
```

### Command-line `.ipa`

Edit `ExportOptions.plist` (`teamID`, `signingStyle`, `method`) and `project.yml`
(`PRODUCT_BUNDLE_IDENTIFIER`, `DEVELOPMENT_TEAM`), then:

```bash
./build-ipa.sh
# → build/NextendoHub.ipa
```

`build-ipa.sh` runs:

```bash
xcodegen generate
xcodebuild -project NextendoHub.xcodeproj -scheme NextendoHub \
  -configuration Release -sdk iphoneos -archivePath build/NextendoHub.xcarchive archive
xcodebuild -exportArchive -archivePath build/NextendoHub.xcarchive \
  -exportOptionsPlist ExportOptions.plist -exportPath build
```

---

## 4. Signing / distribution options

| Method | `ExportOptions.plist` `method` | Notes |
|---|---|---|
| Personal (free) dev cert | `development` | 7-day expiry, needs the device registered; fine for testing. |
| Ad-hoc | `ad-hoc` | Devices listed in the profile; ~1 year. Mirrors what the original ipa was for. |
| TrollStore / AltStore sideload | build `development` or `ad-hoc`, or ad-hoc-sign with `ldid`/`codesign` and add `get-task-allow` (see `NextendoHub.entitlements`) | Same posture as the original (`get-task-allow=true`). |
| App Store / TestFlight | `app-store` | Needs a paid account, a 1024×1024 icon, privacy nutrition labels, and App Review — an unofficial client for a third-party service will likely be rejected. |

---

## 5. Files in this scaffold

```
PLAN.md                     this document
README.md                   quick start
project.yml                 XcodeGen project definition
Info.plist                  bundle metadata (mirrors the original where sensible)
NextendoHub.entitlements    get-task-allow for sideload builds; empty otherwise
ExportOptions.plist         xcodebuild -exportArchive options (edit teamID)
build-ipa.sh                one-shot archive → export → .ipa
Sources/
  NextendoHubApp.swift      @main SwiftUI App
  WebViewController.swift    WKWebView host + shim injection + share sheet
  NextendoBridge.swift       the main.js port (all IPC handlers)
  Keychain.swift             token store (multi-account)
www/
  index.html                YOUR renderer, unchanged
  bridge-shim.js             the preload.js port (defines window.nextendo)
Resources/
  Assets.xcassets/AppIcon.appiconset/   icon-120.png, icon-152.png (from the
        original, decoded) + Contents.json. Add icon-1024.png for the store.
```

---

## 6. Known gaps to finish on-device

1. **App icon set is incomplete** — only 120 & 152 px were recoverable from the
   original. Add at least a 1024×1024 marketing icon and let Xcode 15's single-size
   asset fill the rest, or supply the full set.
2. **`savesDownload`** presents a share sheet; wire it to your preferred
   destination (Files app "Save to Files" is in that sheet by default).
3. **Deep-link / universal-link** for `nextendo://` is intentionally omitted (no
   OAuth in NextendoHub). Add `CFBundleURLTypes` back if you later add web-OAuth.
4. **ATS**: `Info.plist` allows only HTTPS to `nextendo.network` +
   `status.nextendo.network` + `flagcdn.com`. Adjust if endpoints change.
