# Glyph

**An LLM-native programming language where programs are SQLite databases.**

Glyph is a compiled, statically-typed functional language with two foundational design decisions:

1. **Programs are databases, not files.** A `.glyph` file is a SQLite3 database. The unit of source code is the *definition* — a named, hashed row in a SQL table. There are no source files, no imports. An LLM reads context with `SELECT` and writes code with `INSERT`.

2. **Token-minimal syntax.** Every construct is chosen to minimize BPE token count. Single-character type aliases (`I`=Int64, `S`=Str, `B`=Bool), no semicolons, no braces, no `return`/`let`/`import` keywords.

> **Status:** Working self-hosted compiler (v0.2). ~1,100 definitions, C codegen backend, MCP server, full bootstrap chain.

> **Note:** Glyph programs are written by LLMs, not humans. If you're here to contribute, see [Contributing](#contributing).

---

## Requirements

- **Linux** (x86-64)
- **Rust toolchain** — install via [rustup](https://rustup.rs)
- **ninja** — build system (`apt install ninja-build` / `pacman -S ninja`)
- **gcc** or **clang** — C compiler for the final link step
- **sqlite3** — optional, for direct database inspection
- **Claude Code** with an active subscription — to write Glyph programs

---

## Building from source

```bash
git clone <repo>
cd Glyph
ninja                          # build self-hosted glyph compiler
```

`ninja` automatically reconstructs `glyph.glyph` from `src/` if it's missing, then runs the full 3-stage bootstrap chain.

To force a fresh reconstruction from source files:

```bash
ninja reconstruct              # rebuild glyph.glyph from src/
ninja                          # then build the compiler
```

---

## MCP server setup

Glyph ships a built-in MCP (Model Context Protocol) server. Claude uses it to read context and write definitions directly to `.glyph` databases.

### For application development

The system-installed `glyph.glyph` is passed as the MCP argument — it serves as the language documentation and standard library reference. The program being worked on is passed as an additional MCP initialization parameter (JSON). Add to `~/.claude.json`:

```json
{
  "mcpServers": {
    "glyph": {
      "command": "/usr/local/bin/glyph",
      "args": ["mcp", "/usr/local/share/glyph/glyph.glyph"],
      "disabled": false,
      "autoApprove": []
    }
  }
}
```

### For compiler development

Point the server at `glyph.glyph` (the compiler's own source database). Add to `.claude/settings.json` in the Glyph repo:

```json
{
  "mcpServers": {
    "glyph": {
      "command": "./glyph",
      "args": ["mcp", "glyph.glyph"],
      "disabled": false,
      "autoApprove": []
    }
  }
}
```

Available MCP tools: `get_def`, `put_def`, `list_defs`, `search_defs`, `remove_def`, `deps`, `rdeps`, `sql`, `check_def`, `coverage`.

---

## Development workflow

Glyph programs are written by Claude. The human role is to direct Claude, review diffs, run tests, and commit. A typical session:

1. **Open Claude Code** in the Glyph repository with the MCP server configured above.

2. **Ask Claude** to implement a feature or fix a bug — it uses MCP tools to read context and write definitions directly to `glyph.glyph`.

3. **Rebuild** the compiler:
   ```bash
   ninja
   ```

4. **Run tests:**
   ```bash
   ./glyph test glyph.glyph
   ```

5. **Export** definitions to source files:
   ```bash
   ./glyph export glyph.glyph src
   ```

6. **Commit** both the binary and the source files:
   ```bash
   git add src/ glyph.glyph
   git commit
   ```

The `src/` directory holds one `.gl` file per definition, making individual function changes reviewable in GitHub PRs.

---

## Project structure

```
Cargo.toml               workspace root
build.ninja              bootstrap build rules
glyph.glyph              self-hosted compiler (SQLite database, ~1,100 defs)
src/                     compiler source as split .gl files (for git diffs/PRs)
  schema.sql             database schema + extern declarations
  src/<name>.<kind>.gl   gen=1 definitions
  src/gen2/<name>.<kind>.gl  gen=2 definitions (struct codegen overrides)
crates/
  glyph-db/              SQLite schema, custom functions, DB access
  glyph-parse/           indentation-sensitive lexer + recursive-descent parser
  glyph-typeck/          Hindley-Milner type inference + row polymorphism
  glyph-mir/             MIR (flat CFG), lowering, pattern match compilation
  glyph-codegen/         Cranelift codegen, ABI, linker invocation
  glyph-cli/             glyph0 binary — init/build/run/check/test/import
examples/
  calculator/            expression calculator REPL
  glint/                 SQLite project analyzer
  gstats/                statistical analyzer (named record types)
  gled/                  terminal text editor (ncurses)
  life/                  Conway's Game of Life (X11)
  benchmark/             performance comparison vs C
documents/
  glyph-spec.md          formal language specification
  glyph-self-hosted-programming-manual.md  LLM programming manual
```

**Crate dependency order** (no cycles):
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

---

## Bootstrap chain

Glyph is self-hosted. The compiler is written in Glyph and stored in `glyph.glyph`.

```
Stage 0:  glyph0          Rust/Cranelift compiler (cargo build --release)
             │ compiles glyph.glyph --gen=2
             ▼
Stage 1:  glyph1          Cranelift-linked binary
             │ self-builds via C codegen
             ▼
Stage 2:  glyph           final self-hosted compiler (~307k binary)
```

Build commands:

```bash
cargo build --release      # build glyph0 only
ninja                      # full chain: glyph0 → glyph1 → glyph
ninja reconstruct          # rebuild glyph.glyph from src/ files
ninja test                 # run Rust + self-hosted tests
ninja cover                # run tests with coverage instrumentation
ninja install              # install glyph + glyph.glyph to /usr/local
PREFIX=/usr ninja install  # install to /usr instead
ninja -t clean             # remove ninja build artifacts (glyph0, glyph1, glyph, glyph.glyph.cover)
cargo clean                # remove Rust build artifacts (target/)
```

---

## Contributing

### GitHub issues

- **Feature requests** — describe new language features, standard library additions, or tooling improvements
- **Specifications** — provide formal or informal specs for features you'd like implemented; Claude will implement them
- **Bug reports** — include a minimal reproduction: a `.glyph` program or the exact `./glyph put` / `./glyph build` invocation that triggers the bug, plus the error output

### Pull requests

Source definitions live in `src/` as individual `.gl` files — one per definition — making GitHub PRs reviewable at the function level. The `glyph.glyph` binary is committed alongside so the bootstrap chain works from a clean clone.

If you modify `glyph.glyph` (via Claude + MCP), sync and commit:

```bash
./glyph export glyph.glyph src
git add src/ glyph.glyph
git commit
```
