# Maintainer-only CI samples

Files here are **paste-ready workflows for maintainers**, not end-user docs.

| File | Purpose |
| --- | --- |
| [linux-gui.yml](linux-gui.yml) | Linux Qt GUI build + `gui_smoke` (offscreen) — paste under `jobs:` in `.github/workflows/ci.yml` |
| [release.yml](release.yml) | Build Windows portable zip on `v*` tags and attach to the GitHub Release |

Do **not** treat these as something a modder runs. Preferred live path is `.github/workflows/` when the pushing token has `workflow` scope; otherwise paste from here in the GitHub UI.

**Residual blocker:** OAuth tokens without `workflow` scope cannot push `.github/workflows/*`. Until a scoped token (or UI paste) promotes these files, live CI stays `linux-cli` + `windows-gui` only — Linux GUI smoke is documented and proven locally, but **not** on every PR. See [building.md](../building.md#ci) and issues [#52](https://github.com/tofb15/sxpe/issues/52) / [#54](https://github.com/tofb15/sxpe/issues/54).
