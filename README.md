# NextendoHub-nx

Native Nintendo Switch homebrew client for nextendo.network — the Switch port of
NextendoHub. **Build needs devkitPro + libnx** (see `PLAN.md` for why a `.nro`
can't be produced on Windows without it).

## Quick start (with devkitPro installed)

```bash
(dkp-)pacman -S switch-dev switch-curl switch-mbedtls switch-zlib
cd NextendoHub-nx
make            # -> NextendoHub.nro
```

Put `NextendoHub.nro` on the SD card under `sdmc:/switch/` and run it from the
Homebrew Menu. For network access, launch it via a full hbmenu / title-takeover
(hold R on a game), not applet mode.

## Controls

- **L / R** — switch screen (Server status · In game now · Friends)
- **X** — refresh
- **Y** — sign in (on the Friends screen, when signed out) — on-screen keyboard
- **−** (Minus) — sign out
- **+** (Plus) — exit

## Status

Done: init, TLS-verified HTTP (`httpJson`), SD token store, swkbd login, and the
three read-only screens. Everything else (profile edit, saves, avatar gallery,
multi-account, a real GUI) re-uses `httpJson()` — the endpoint list and the
recommended `borealis` GUI library are in `PLAN.md`.
