# Maintainer-only CI samples

Files here are **paste-ready workflows for maintainers**, not end-user docs.

| File | Purpose |
| --- | --- |
| [linux-gui.yml](linux-gui.yml) | Linux Qt GUI build + `gui_smoke` (offscreen) - paste under `jobs:` in `.github/workflows/ci.yml` |
| [release.yml](release.yml) | Synced copy of live `.github/workflows/release.yml` (Windows zip + Linux CLI tarball on `v*` tags) |

Do **not** treat these as something a modder runs. Preferred live path is `.github/workflows/` when the pushing token has `workflow` scope; otherwise paste from here in the GitHub UI.

**Residual blocker:** OAuth tokens without `workflow` scope cannot push `.github/workflows/*`. Live `ci.yml` stays `linux-cli` + `windows-gui` until a scoped token (or UI paste) promotes `linux-gui.yml`. Release automation lives in `.github/workflows/release.yml` (synced from this folder). See [building.md](../building.md#ci).

Release notes file is `docs/releases/<tag>.md` (e.g. `v0.8.0.md` for tag `v0.8.0`); the release workflow uses it when present, otherwise generates notes.
