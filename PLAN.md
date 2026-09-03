# NextendoHub → Switch homebrew (`.nro`) — plan & build

## Why there's no `.nro` in this download

A `.nro` is a **Nintendo Switch homebrew executable**: an ELF for aarch64 wrapped
by `elf2nro`, with an `.nacp` (title/author/version) and a JPEG icon. Producing
one requires **devkitPro** (`devkitA64` cross-GCC + **libnx** + `elf2nro` /
`nacptool`). That toolchain is not on this Windows machine, and — as with the
`.ipa` — you cannot cross-build it from a normal Windows box without it.

Electron/HTML also can't run under homebrew: no Node, and you can't bundle a
browser engine. So NextendoHub becomes a **native libnx app**, not a repackage.

## The two ways to port it

| | Native libnx UI (recommended) | Web-applet hybrid |
|---|---|---|
| How | Draw the UI yourself. Text console (this scaffold) or a real GUI with **borealis** / **deko3d + nanovg**. Networking via **libcurl** (this scaffold). | Ship the HTML in `romfs`, run a tiny HTTP server inside the `.nro`, and open the Switch **web-browser applet** (`libnx` `web`/`WebSession`) at `http://127.0.0.1:<port>`; a shim posts to the local server which does the real requests. |
| Renderer reuse | none — rewrite screens | almost 100% (`www/index.html` unchanged, `bridge-shim.js` talks to the local server) |
| Reliability | solid; this is how most Switch homebrew is built | fragile — the browser applet is heavily locked down (whitelist, no reliable `127.0.0.1`, no persistent storage, JS gaps). Often needs the "internet browser" hidden applet and specific firmware. Treat as experimental. |
| Effort | medium–large (port every screen) | small if the applet cooperates, otherwise a dead end |

This scaffold takes the **native** path with a **console UI** so it builds with
just `libnx + switch-curl` (no GUI-library dependency) and already does something
real.

## What the scaffold already does

`source/main.cpp` is a working libnx client:

- `socketInitializeDefault()` + **libcurl** (mbedTLS backend) with proper TLS
  verification against `romfs:/cacert.pem` (Mozilla CA bundle, bundled).
- `httpJson(method, path, body, bearer)` — the `window.nextendo.*` bridge / the
  Swift `NextendoBridge` / the Electron `nxRequest`, ported to C.
- Token store on the SD card at `sdmc:/switch/nextendo-hub/session.dat`
  (the Switch has no Keychain/DPAPI — see "Security" below).
- **swkbd** on-screen keyboard login → `POST /api/login` → token saved.
- Three read-only screens, L/R to switch, X to refresh, + to exit:
  - **Server status** — `status.nextendo.network` status-page + heartbeat, monitor list.
  - **In game now** — `/api/online-counts`, per-game player counts (works with **no account**).
  - **Friends** — `/api/friends` with the bearer token; online / in-game / offline.

## What's left (all re-use `httpJson` the same way)

- Profile: `GET/PUT /api/profile` (full-document PUT, like the desktop fix),
  `PUT /api/username`, `GET/POST /api/country`.
- Cloud saves: `GET /api/saves`, `DELETE /api/save/<id>`, and download
  (`GET /api/save/<id>` → write to `sdmc:/switch/nextendo-hub/saves/…zip`).
- Avatar gallery: `GET /assets/avatars/manifest.json` + PNG bytes.
- Multi-account, register (`POST /api/register`).
- A real GUI: swap the `printf` console for **borealis**
  (`https://github.com/natinusala/borealis`) — it renders a Switch-style UI and
  maps cleanly onto the tab layout (Status / Online / Friends / Settings).

## Build

Install devkitPro (Windows: the graphical installer + MSYS2; or use the
`devkitpro/devkita64` Docker image), then:

```bash
(dkp-)pacman -S switch-dev switch-curl switch-mbedtls switch-zlib
cd NextendoHub-nx
make                     # -> NextendoHub.nro
```

Copy `NextendoHub.nro` to `sdmc:/switch/NextendoHub/NextendoHub.nro` and launch
it from the Homebrew Menu (album / title-redirect). Needs CFW (Atmosphère) or a
hbmenu entrypoint; homebrew has no networking in applet mode without the sysmodule
— run it as a **title takeover** (hold R on a game) for full socket access, or
from a full hbmenu launch.

## Files

```
Makefile              standard libnx application Makefile (romfs + curl linked)
source/main.cpp       the client (init, httpJson, token store, swkbd, 3 screens)
source/cJSON.c/.h     vendored JSON parser (MIT, DaveGamble/cJSON v1.7.18)
romfs/cacert.pem      Mozilla CA bundle for curl TLS verification
icon.jpg              256x256 app icon
```

## Security note (Switch has no Keychain)

On desktop the token is OS-encrypted (DPAPI / Keychain) and per-user. The Switch
SD card has **no equivalent** — `session.dat` is plaintext on the card, readable
by anything that can mount the SD (this homebrew, other homebrew, a PC). Mitigations:

- Store only the short-lived `token` (not the password); it's revocable server-side.
- Optionally XOR/AES it with a key derived from a device-unique value
  (`setsysGetSerialNumber`) so a card pulled into another console is useless — add
  in `tokenLoad/tokenSave`.
- Offer a "sign out on exit" toggle.

## Attribution

Made by **adxmm**. Founders of Nextendo Network: **JuanBrew**, **Kazu**.
Unofficial client — not affiliated with Nintendo or Nextendo Network.
