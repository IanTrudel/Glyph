# Glyph Distribution

## Current State

```
git clone → cargo build (4-stage bootstrap) → sudo ninja install
```

Artifacts are small: `glyph` (~374k) + `glyph.glyph` (~1.4MB) — the entire distribution is ~2MB.

Runtime dependencies after install: `cc` (or `clang`) and `llc` (LLVM backend). SQLite is statically linked.

## Target User

The install story is for a human who needs to set Glyph up once so an LLM can use it:

1. Human installs `glyph` (one-time)
2. Human adds MCP config to `~/.claude.json` (one-time)
3. LLM writes programs via MCP indefinitely

This means install must be simple, fast, and require no Rust toolchain.

## Options

### Option A: GitHub Releases + install script (recommended 80/20)

CI workflow builds on tag push, uploads `glyph-linux-x86_64.tar.gz`. An install script:

```sh
curl -fsSL https://example.com/install.sh | sh
```

Downloads tarball, extracts to `/usr/local/bin/glyph` and `/usr/local/share/glyph/glyph.glyph`. No Rust, no ninja, done in ~10 seconds.

The `glyph` binary grows a `glyph update` subcommand that hits the GitHub releases API and atomically replaces itself. Since the binary is self-hosted Glyph, this is a Glyph program.

### Option B: Platform packages

AUR PKGBUILD (relevant for Arch Linux), `brew` formula for Mac. Gives proper package manager integration (`pacman -S glyph`, updates via `pacman -Syu`). Downside: maintenance burden per platform.

### Option C: A "glyphup" tool

Full rustup analogue — a separate binary managing Glyph versions. Probably over-engineering given the small audience.

## Recommended Implementation (three steps)

**1. `glyph --version`**
Embed a version string at build time (git tag + commit hash). Currently there is no version. Required before any of the rest makes sense.

**2. GitHub Actions CI**
On tag push: run the full 4-stage bootstrap, upload `glyph` + `glyph.glyph` as paired release artifacts. The Rust build is the bottleneck; subsequent stages are fast.

**3. `glyph update` command**
Self-hosted Glyph subcommand that:
- Fetches latest GitHub release JSON
- Compares version string against current
- Downloads updated `glyph` binary + `glyph.glyph` if newer
- Atomically replaces both (download to temp, rename on success)

This gives the rustup experience for the 2MB case: `curl | sh` to install, `glyph update` to stay current.

## Key Constraint: Paired Versioning

`glyph` (binary) and `glyph.glyph` (compiler source/stdlib) must always match — you cannot update one without the other. The solution: always distribute and update them as a single matched pair. `glyph update` downloads both atomically.

## Open Question

Is the goal making it easier to set up on a new machine (personal use), or genuine public distribution?

---

## Refined Design (Option A)

### Install target: user-local, not system

Like rustup (`~/.cargo/bin/`), not `sudo ninja install` (`/usr/local/`). No root required.

```
~/.glyph/
  bin/
    glyph           ← binary
  share/
    glyph.glyph     ← compiler source / stdlib
```

Install script appends `~/.glyph/bin` to `$PATH` in `~/.bashrc` / `~/.zshrc`. The MCP server config points to `~/.glyph/share/glyph.glyph`.

### Versioning: meta table, not a definition

`glyph.glyph` already has a `meta(key, value)` table used for `schema_version`. The version lives there too:

```sql
INSERT OR REPLACE INTO meta (key, value) VALUES ('version', 'v0.3.0');
```

Benefits over a `glyph_version` function definition:
- Doesn't touch the dependency graph or trigger recompilation
- Queryable with `./glyph sql glyph.glyph "SELECT value FROM meta WHERE key='version'"`
- Updated by the ninja build via a single SQL statement (no `put`, no rebuild)
- `glyph update` can compare versions with a direct DB query on the downloaded glyph.glyph

`glyph --version` reads `meta` at startup: one SQL query, print, exit.

The ninja build injects the version before stage 3:
```
git describe --tags --always  →  ./glyph sql glyph.glyph "INSERT OR REPLACE INTO meta..."
```

### Implementation order

1. **meta table version** — add `version` key to `meta` in `glyph.glyph`; handle `--version` flag in `dispatch_cmd`
2. **GitHub Actions workflow** — trigger on `v*` tag; build full 4-stage chain; upload `glyph-linux-x86_64.tar.gz` containing `glyph` + `glyph.glyph`
3. **Install script** — `curl | sh`; creates `~/.glyph/{bin,share}`; extracts tarball; patches PATH; prints MCP config snippet
4. **`glyph update`** — hits GitHub releases API with `curl` (via `glyph_system`); parses JSON with existing JSON subsystem; downloads to `/tmp/`; atomically replaces `~/.glyph/bin/glyph` and `~/.glyph/share/glyph.glyph`

### `glyph update` finding its own install path

The binary locates itself via `/proc/self/exe` (Linux), derives paths:
- Binary: `readlink /proc/self/exe`
- glyph.glyph: `dirname(binary)/../share/glyph.glyph`

This works correctly whether installed to `~/.glyph/` or `/usr/local/` (for those who still prefer system install).

---

## Implementation Status (2026-03-20)

Option A is implemented and tested locally. Awaiting a public GitHub repo to test end-to-end.

### What is done

**`glyph_version` function** (`glyph.glyph`):
- `glyph_version u = "dev"` — default for local/dev builds
- CI stamps the release tag before `ninja bootstrap` via:
  ```sh
  sqlite3 glyph.glyph "UPDATE def SET body='glyph_version u = \"$VERSION\"', compiled=0 WHERE name='glyph_version' AND kind='fn';"
  ```
- `--full` flag in Stage 1 recomputes hash/tokens, so the stale hash from the SQL update is fine

**CLI commands** (`glyph.glyph`):
- `glyph --version` and `glyph version` both print `glyph <version>` and exit 0
- `glyph update` fetches the latest GitHub release, compares version, downloads and atomically replaces `glyph` binary + `glyph.glyph`
- Path derivation: `argv[0]` → `dirname` → `/../share/glyph.glyph` (works for any install prefix)
- Dispatch chain: `dispatch_cmd2` → `dispatch_cmd3` (handles `--version`, `version`, `update`)

**New definitions in `glyph.glyph`** (9 new, 3 modified):
- `glyph_version`, `glyph_repo` — version string and repo placeholder
- `cmd_version`, `cmd_update` — command handlers
- `glyph_dirname`, `glyph_dirname_loop` — path helpers
- `update_fetch_release`, `update_find_asset_loop` — GitHub API + JSON parsing
- `dispatch_cmd3` — extended dispatch chain
- Modified: `dispatch_cmd2` (routes unknown to `dispatch_cmd3`), `print_usage` (added two lines)

**`.github/workflows/release.yml`**:
- Triggers on `v*` tag push
- Installs `ninja-build llvm clang sqlite3` + Rust stable
- Stamps version, runs `ninja bootstrap`, packages `glyph` + `glyph.glyph` into `glyph-linux-x86_64.tar.gz`
- Uploads as GitHub Release asset via `softprops/action-gh-release@v2`

**`install.sh`**:
- curl-pipeable: `curl -fsSL .../install.sh | sh`
- Installs to `~/.glyph/bin/glyph` and `~/.glyph/share/glyph.glyph`
- Patches `~/.bashrc` and `~/.zshrc` if not already patched
- Prints MCP config snippet on completion

### What needs doing before first release

1. Make repo public and fill in `OWNER/REPO` in three places:
   - `glyph_repo` function in `glyph.glyph`
   - `REPO=` variable in `install.sh`
   - The `curl` URL in the README install instructions
2. Tag `v0.2.0` — the workflow triggers automatically
3. Verify `glyph update` end-to-end against a real release

### Design note: function def vs meta table

The refined design above proposed storing version in the `meta` table (avoids touching the dep graph). The implementation chose a `glyph_version` function instead because:
- The version must be readable at runtime without opening a second database connection
- The `--full` rebuild already recomputes all hashes, so dirtying one def is free
- The meta table approach would require a SQL query at startup before the command is dispatched

The meta table approach remains valid and may be worth revisiting if the startup overhead ever matters.
