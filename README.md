# NextendoHub-nx

The **full** NextendoHub app as native Nintendo Switch homebrew — borealis GUI
(Switch-native look, themed, real fonts), every feature of the desktop build.

**Build needs devkitPro + libnx + borealis** — a `.nro` can't be produced on
Windows without the toolchain. See `PLAN.md`.

## Quick start

```bash
(dkp-)pacman -S switch-dev switch-curl switch-mbedtls switch-zlib \
               switch-glfw switch-mesa switch-libdrm_nouveau
cd NextendoHub-nx
./setup.sh        # once: git clone borealis (legacy branch) + stage resources
make             # -> NextendoHub.nro
```

Or push this repo to GitHub — `.github/workflows/build-nro.yml` builds it in
CI (the official `devkitpro/devkita64` container) and uploads `NextendoHub.nro`
as a workflow artifact on every push.

Put `NextendoHub.nro` under `sdmc:/switch/` and launch from the Homebrew Menu via
a full launch / title-takeover (hold R on a game) so it has network.

## Tabs

- **In game now** — live per-game player counts (15s, no account needed)
- **Server status** — status.nextendo.network monitors, up/down/ping (60s)
- **Friends** — login / register / multi-account · presence + game names ·
  requests (accept/decline) · add by friend code
- **Settings** — theme (system/light/dark) · language (en/fr/es) · profile
  (username, country, avatar gallery, colour) · cloud saves (download to SD /
  delete) · favourite mods · Made-by credit

**X** = refresh the current tab.

## Status

Everything from the exe is wired (see the parity table in `PLAN.md`). Not
compiled here — expect a brief shakeout pass on a devkitPro machine.
