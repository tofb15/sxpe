# SXPE documentation

Start at the **[repository README](../README.md)** for what SXPE is, why it exists, and how to get a build. This folder is the rest of the manual. Pick **one** page for the job; do not copy install steps or limit lists into new files.

## By reader

| I am… | Read |
| --- | --- |
| Discovering the project | [README](../README.md) |
| Using the desktop GUI | [user-guide.md](user-guide.md) |
| Doing a named mod task | [workflows.md](workflows.md) |
| Writing scripts or agent tools | [cli-mcp.md](cli-mcp.md), then [spec/catalog.md](spec/catalog.md) |
| Compiling or packaging | [building.md](building.md) |
| Cutting a release / bumping X.Y.Z | [versioning.md](versioning.md), then [building.md](building.md#releases) |
| Implementing a codec | [spec/](spec/README.md) |
| Changing architecture | [DESIGN.md](../DESIGN.md) |
| Sending a patch | [CONTRIBUTING.md](../CONTRIBUTING.md) |

## Canonical homes (do not duplicate)

| Topic | Source of truth |
| --- | --- |
| What / why / vs s3pe / vision / download | [README](../README.md) |
| GUI menus, editors, shortcuts, Check for update | [user-guide.md](user-guide.md) |
| Step-by-step recipes (merge, S3SA, scan, …) | [workflows.md](workflows.md) |
| Bus envelopes, CLI flags, MCP names | [cli-mcp.md](cli-mcp.md) |
| Full command table | [spec/catalog.md](spec/catalog.md) |
| Configure / build / test / zip / CI | [building.md](building.md) |
| When to bump MAJOR.MINOR.PATCH | [versioning.md](versioning.md) |
| Neighborhood layout lock | [neighborhood-layout.md](neighborhood-layout.md) |
| Optional FullBuild/CC round-trip | [testing.md](testing.md) |
| Release notes for a tag | [releases/](releases/TEMPLATE.md) |
| Maintainer workflow YAML | [ci/](ci/README.md) |

## Specialist (not the landing path)

- [images/](images/) - README screenshots only (no `.package` / CC / personal paths)
- [tags.md](tags.md) - what each resource Tag stores
- [preview.md](preview.md) - inspector preview inventory
- [neighborhood-layout.md](neighborhood-layout.md) - `.nhd` / `.world` / `.dbc`
- [testing.md](testing.md) - gitignored game-file harness
