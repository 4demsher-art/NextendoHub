# NextendoHub → Switch homebrew (`.nro`) — full port

## Why there's no `.nro` in this download

Building a `.nro` needs **devkitPro** (`devkitA64` + **libnx** + `elf2nro`/`nacptool`),
not installed on this Windows box — and you can't cross-build it from Windows
without it. Electron/HTML can't run under homebrew either (no Node, can't bundle a
browser), so this is a native **libnx + borealis** rewrite, not a repackage.

## What this is

The **whole exe app**, ported. borealis draws the UI (Switch-native look: themed
header/footer, list rows, sections, dropdowns, dialogs, on-screen keyboard, auto
light/dark, real fonts). `source/net.cpp` is the entire `window.nextendo.*`
bridge — every `ipcMain.handle` from the desktop `main.js` has an equivalent.

### Feature parity

| Desktop feature | Switch |
|---|---|
| **Servers tab** — Uptime Kuma status, banner, per-group monitors, 60s poll | ✅ `LiveList`, worker-thread fetch, rebuild on UI thread, **X** to refresh |
| **Online tab** — in-game player counts, 15s poll | ✅ `LiveList` |
| **Login / Register** | ✅ multi-field via `brls::Swkbd` chained; password rules + confirm |
| **Multi-account** — switcher, add, sign out | ✅ `config.json` on SD, `brls::Dropdown` switcher |
| **Friends** — presence, game names, favourites sort, tally | ✅ `/api/friends` + `/api/gameinfo` resolution |
| **Friend requests** — accept / decline | ✅ row → `brls::Dialog` |
| **Add friend by code** | ✅ swkbd |
| **Profile: username** — live availability + `PUT /api/username` | ✅ swkbd + `usernameAvailable()` |
| **Profile: country (MK8 flag)** | ✅ `brls::Dropdown` from `/api/country` (ISO codes) → `POST /api/country` |
| **Profile: picture — gallery of 165 Switch avatars** | ✅ `/assets/avatars/manifest.json` → list → pick → PNG bytes → `PUT /api/profile` (`image` base64 + `avatar:{"char":…}`) |
| **Profile: colour swatches** | ✅ dropdown of the 12 hex colours → `PUT /api/profile` |
| **Cloud saves** — quota, download, delete, gate messages | ✅ `/api/saves`; download → `sdmc:/switch/nextendo-hub/saves/*.zip`; delete via dialog |
| **Favourite mods** | ✅ `/api/mod-favorites` (best effort) |
| **Settings: theme** (system/light/dark) | preference stored, but this borealis build has no runtime setter — see note below |
| **Settings: language** (en/fr/es) | ✅ `i18n.hpp`, persisted; reopen tabs to fully re-translate |
| **Settings: launch at startup** | shown as **"not applicable on Switch"** |
| **"Made by adxmm / Founders JuanBrew · Kazu" credit** | ✅ About section; friend code auto-captured when adxmm signs in |
| **Full-document `PUT /api/profile`** (the desktop persistence fix) | ✅ ported in `net::profileSave` |
| **XSS hardening** | N/A — no HTML renderer; borealis draws text, not markup |
| Tray / close-to-tray | N/A on Switch |
| Profile **photo upload** (file picker) | not ported — needs an SD file browser + PNG decode/JPEG encode. Gallery covers avatars. |
| Live async everywhere | Status/Online are async; the account screens fetch synchronously on open (brief pause) — move to a worker for polish. |

## Build

```bash
# devkitPro installed (Windows graphical installer + MSYS2, or the
# devkitpro/devkita64 Docker image):
(dkp-)pacman -S switch-dev switch-curl switch-mbedtls switch-zlib \
               switch-glfw switch-mesa switch-libdrm_nouveau

cd NextendoHub-nx
./setup.sh          # once — git clone borealis, stage its resources into romfs/
make               # -> NextendoHub.nro
```

Or push this to GitHub and let `.github/workflows/build-nro.yml` build it —
see the repo root README for the CI setup.

Put `NextendoHub.nro` under `sdmc:/switch/NextendoHub/` and launch from the
Homebrew Menu via a **full launch / title-takeover** (hold R on a game) so it has
socket access (applet-mode hbmenu has no network).

> Not compiled here (no devkitPro on Windows). `source/main.cpp` is written
> against borealis's **`legacy` branch** API (the classic GLFW+GL renderer:
> `brls::List`/`ListItem`/`TabFrame`/`Swkbd`/`Dropdown`/`Dialog`), which
> `setup.sh` pins to a verified commit. borealis's `main` branch is a
> different, in-progress deko3d/yoga rewrite missing most of that API — an
> earlier version of this project mistakenly pinned `main` and failed to
> build; if you ever repoint `setup.sh` at a different ref, repoint it at
> `legacy`, not `main`. One known gap even on `legacy`: there's no runtime
> theme setter (`Application::setThemeVariant` doesn't exist there either) —
> theme is decided once at `Application::init()` before prefs can even be
> read, so the Settings theme picker stores a preference but has no visible
> effect on Switch; see the `theme_na` string in `source/i18n.hpp`.

## Files

```
setup.sh              pulls borealis + stages resources (run once)
Makefile              libnx app Makefile; includes lib/borealis/library/borealis.mk
source/main.cpp       borealis UI — LiveList + all four tabs, every screen
source/net.hpp/.cpp   full window.nextendo.* bridge (libcurl + multi-account store + all endpoints)
source/i18n.hpp       en / fr / es strings
source/cJSON.c/.h     vendored JSON parser (MIT)
romfs/cacert.pem      Mozilla CA bundle for curl TLS verification
icon.jpg              256x256 app icon
lib/borealis/         created by setup.sh
```

## Security (the Switch has no Keychain)

Desktop keeps the token OS-encrypted (DPAPI / Keychain), per-user. The SD card has
no equivalent — `sdmc:/switch/nextendo-hub/config.json` stores the bearer token in
plaintext, readable by any homebrew or a PC. Mitigations: store only the
short-lived revocable token (done — never the password); optionally XOR/AES it with
a key from `setsysGetSerialNumber()` in `net::cfgLoad/cfgSave`; a "sign out on
exit" toggle.

## Attribution

Made by **adxmm**. Founders of Nextendo Network: **JuanBrew**, **Kazu**.
Unofficial — not affiliated with Nintendo or Nextendo Network.
