# Glyph

**A programming language written by an LLM, for LLMs.**

Glyph is a compiled, statically-typed functional language designed from first principles for LLM authorship. You don't write Glyph — you direct an LLM that does. The compiler itself was written in Glyph by Claude, and is stored as a Glyph program.

Two design decisions follow from this premise:

1. **Programs are databases, not files.** A `.glyph` file is a SQLite3 database. The unit of source code is the *definition* — a named, hashed row in a SQL table. There are no source files, no imports. An LLM reads context with `SELECT` and writes code with `INSERT`.

2. **Token-minimal syntax.** Every construct is chosen to minimize BPE token count. Single-character type aliases (`I`=Int64, `S`=Str, `B`=Bool), no semicolons, no braces, no `return`/`let`/`import` keywords.

The MCP server gives Claude structured tools to navigate the program graph, query definitions, check types, and write code directly into the database. The compiler's own source — stored in the same format — serves as the language reference.

> **Status:** Working self-hosted compiler (v0.2). ~1,363 definitions, C codegen + LLVM IR backends, MCP server (15 tools), 4-stage bootstrap chain.

> **Note:** Glyph programs are written by LLMs, not humans. If you're here to contribute, see [Contributing](#contributing).

---

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/IanTrudel/Glyph/main/install.sh | sh
```

Installs the `glyph` binary to `~/.glyph/bin/`, the Claude Code skill to `~/.claude/skills/glyph/`, and auto-configures the MCP server in `~/.claude/settings.json`.

**Requirements:** `clang` or `gcc`, `llvm` (for `--emit=llvm`), `python3` (for MCP auto-config).

---

## Hello, World!

With the Glyph MCP configured (see [MCP server setup](#mcp-server-setup)), prompt Claude:

> Create hello.glyph and write a hello world program in Glyph.

Claude uses the `init` MCP tool to create the database, then `put_def` to insert a `main` definition. Then build and run:

```bash
glyph run hello.glyph
# Hello, World!
```

To build a standalone executable:

```bash
glyph build hello.glyph hello
./hello
# Hello, World!
```

See `examples/hello/hello.glyph` for the resulting database.

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
git clone https://github.com/IanTrudel/Glyph
cd Glyph
ninja                          # build self-hosted glyph compiler
sudo ninja install
```

`ninja` automatically reconstructs `glyph.glyph` from `src/` if it's missing, then runs the full 4-stage bootstrap chain.

## MCP server setup

Glyph ships a built-in MCP server. Claude uses it to explore the codebase, write and validate definitions, build, and run — all without touching files directly.

Pass the database you're working on as the startup argument; it becomes the default for all tool calls:

```json
{
  "mcpServers": {
    "glyph": {
      "command": "/usr/local/bin/glyph",
      "args": ["mcp", "/usr/local/share/glyph/glyph.glyph"]
    }
  }
}
```

The `db` argument is optional. Omit it and pass `db=` explicitly on each tool call instead — useful when working across multiple databases in the same session.

### MCP tools

| Tool | Description |
|------|-------------|
| `init` | Create a new `.glyph` database |
| `get_def` | Read a definition by name and kind |
| `put_def` | Insert or replace a definition (validates before inserting) |
| `check_def` | Validate a definition body without inserting |
| `remove_def` | Delete a definition |
| `list_defs` | List definitions, optionally filtered by kind |
| `search_defs` | Search definition bodies with a `LIKE` pattern |
| `deps` | Forward dependencies of a definition |
| `rdeps` | Reverse dependencies (what calls it) |
| `sql` | Raw SQL — full access to the database schema |
| `build` | Compile a `.glyph` database to a native executable |
| `run` | Build and execute, returning stdout |
| `coverage` | Function-level coverage from the last `glyph test --cover` run |
| `link` | Copy definitions from a library database into an application |
| `migrate` | Apply pending schema migrations to a database |

---

## Development workflow

Claude writes all Glyph code via MCP. The human role is to direct it, run builds, and commit.

1. **Configure the MCP server** pointing at your application database (see above) and open Claude Code.

2. **Direct Claude** — describe what to build. Claude creates the database if needed (`init`), then explores, writes, and validates definitions via MCP tools. No manual file editing.

3. **Run:**
   ```bash
   glyph run myapp.glyph
   ```

4. **Test** (if your app has test definitions):
   ```bash
   glyph test myapp.glyph
   ```

5. **Commit:**
   ```bash
   git add myapp.glyph
   git commit
   ```

To build a standalone executable:
```bash
glyph build myapp.glyph myapp
./myapp
```

---

## Bootstrap chain

Glyph is self-hosted. The compiler is written in Glyph and stored in `glyph.glyph`.

```
Stage 0:  glyph0          Rust/Cranelift compiler (cargo build --release)
             │ compiles glyph.glyph → Cranelift binary
             ▼
Stage 1:  glyph1          Cranelift-linked binary
             │ self-builds via C codegen
             ▼
Stage 2:  glyph2          C-codegen binary
             │ re-builds via LLVM IR codegen
             ▼
Stage 3:  glyph           final self-hosted compiler (LLVM-compiled)
```

Each stage validates the previous one: glyph2 proves C codegen works, glyph proves LLVM codegen works and can reproduce itself.

---

## Contributing

### GitHub issues (RECOMMENDED)

- **Feature requests** — describe new language features, standard library additions, or tooling improvements
- **Specifications** — provide formal or informal specs for features you'd like implemented; Claude will implement them
- **Bug reports** — include a minimal reproduction: a `.glyph` program or the exact `./glyph put` / `./glyph build` invocation that triggers the bug, plus the error output
