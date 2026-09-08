# Maintainer-only CI samples

Files here are **paste-ready workflows for maintainers**, not end-user docs.

| File | Purpose |
| --- | --- |
| [release.yml](release.yml) | Build Windows portable zip on `v*` tags and attach to the GitHub Release |

Do **not** treat these as something a modder runs. Preferred live path is `.github/workflows/` when the pushing token has `workflow` scope; otherwise paste from here in the GitHub UI. See [building.md](../building.md#github-actions-release-workflow).
