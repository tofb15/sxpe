# Maintainer-only CI samples

Files here are **paste-ready workflows for maintainers**, not end-user docs.

| File | Purpose |
| --- | --- |
| [linux-gui.yml](linux-gui.yml) | Linux Qt GUI build + `gui_smoke` (offscreen) - paste under `jobs:` in `.github/workflows/ci.yml` |
| [release.yml](release.yml) | Canonical four-artifact release workflow (`v*` + `workflow_dispatch`); paste into `.github/workflows/release.yml` when `workflow` scope allows |

Do **not** treat these as something a modder runs. Preferred live path is `.github/workflows/` when the pushing token has `workflow` scope; otherwise paste from here in the GitHub UI.

**Residual blocker:** OAuth tokens without `workflow` scope cannot push `.github/workflows/*`. Live `ci.yml` stays `linux-cli` + `windows-gui` until a scoped token (or UI paste) promotes `linux-gui.yml`. Live `.github/workflows/release.yml` still publishes only the previous two assets until this folder's `release.yml` is pasted (or pushed with `workflow` scope). See [building.md](../building.md#ci).

Release notes file is `docs/releases/<tag>.md` (e.g. `v0.8.0.md` for tag `v0.8.0`); the release workflow resolves `${GITHUB_REF_NAME}` / the `workflow_dispatch` `tag` input and uses that file when present, otherwise generates notes. Softprops does **not** force `prerelease:` — leave Beta marking to `gh release edit` after upload.

**Release matrix (four artifacts):**

| Artifact | Job | Notes |
| --- | --- | --- |
| `sxpe-<ver>-windows-x64.zip` | `windows-portable` | GUI + CLI + MCP + Qt |
| `sxpe-<ver>-windows-x64-cli.zip` | `windows-cli` | CLI + MCP via `package.ps1 -NoGui` |
| `sxpe-<ver>-linux-x64-cli.tar.gz` | `linux-cli` | `package-linux.sh --no-gui` |
| `sxpe-<ver>-linux-x64.tar.gz` | `linux-gui` | Qt install like [linux-gui.yml](linux-gui.yml); Qt **not** vendored |
