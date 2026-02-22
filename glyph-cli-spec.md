# Glyph CLI Specification v0.1

**The command-line interface to the Glyph program image.**

---

## 1. Philosophy

The Glyph CLI treats a `.glyph` database the way Smalltalk treats its image:
a **living program** you interact with through commands, not files you edit.
The SQLite layer is an implementation detail. An LLM should never need to
write SQL for normal development.

### 1.1 Design Principles

| Principle               | Rationale                                              |
|-------------------------|--------------------------------------------------------|
| **Verbs, not queries**  | `glyph get process` not `SELECT ... WHERE name = ...`  |
| **Stdout is structured**| All output is parseable: default compact, `--json` available |
| **Exit codes are semantic** | 0=ok, 1=not found, 2=type error, 3=compile error, 4=runtime error |
| **Implicit target**     | Commands operate on `./program.glyph` by default       |
| **Pipeable**            | Output of one command is valid input to another         |
| **Idempotent writes**   | `glyph put` with identical content is a no-op          |
| **Atomic**              | Every command is a single SQLite transaction            |

### 1.2 Global Flags

```
-d, --db <path>     Target database (default: ./program.glyph or $GLYPH_DB)
-q, --quiet         Suppress non-essential output
-j, --json          JSON output for all read commands
-v, --verbose        Include metadata (hash, tokens, timestamps)
--color <mode>       auto|always|never (default: auto)
```

### 1.3 Output Conventions

Default output is **compact text**, optimized for LLM token budgets:

```
$ glyph get process
fn process items = items |> filter .valid |> map transform |> collect

$ glyph get process --json
{"name":"process","kind":"fn","sig":"[Item] -> [Item]","body":"process items = ...","tokens":12}
```

When multiple definitions are returned, they are separated by a blank line.
In JSON mode, they are a JSON array.

---

## 2. Command Reference

### Overview

```
LIFECYCLE           QUERY & NAVIGATE        MODIFY
─────────           ────────────────        ──────
glyph init          glyph get               glyph put
glyph build         glyph ls                glyph edit
glyph run           glyph find              glyph rm
glyph check         glyph deps              glyph mv
glyph test          glyph rdeps             glyph tag
glyph repl          glyph diff              glyph extern
                    glyph stat
                    glyph dump              ADVANCED
                    glyph cat               ────────
                                            glyph sql
                                            glyph attach
                                            glyph snapshot
                                            glyph restore
                                            glyph migrate
```

---

### 2.1 LIFECYCLE

#### `glyph init [path]`

Create a new program image.

```
$ glyph init myapp.glyph
Created myapp.glyph (schema v0.1, stdlib attached)

$ glyph init                    # creates ./program.glyph
Created program.glyph (schema v0.1, stdlib attached)

$ glyph init --bare             # no stdlib
Created program.glyph (schema v0.1, bare)
```

The stdlib is attached as a separate database, not copied in.

---

#### `glyph build [--full] [--target <triple>]`

Compile all dirty definitions.

```
$ glyph build
compiled 3/47 defs (44 cached) in 82ms

$ glyph build --full
compiled 47/47 defs in 340ms
```

Output on error:
```
$ glyph build
ERR process:4 type mismatch: expected [Item], got ?[Item]
    |> filter .valid
    ^^^^^^^^^^^^^^^^^
    this returns ?[Item] because `filter` may fail
hint: add ? to propagate or ! to unwrap
1 error, 0 warnings
```

Exit codes: 0 = success, 2 = type errors, 3 = other compile errors.

---

#### `glyph run [-- args...]`

Build (if dirty) and execute `main`.

```
$ glyph run -- --color data/*.csv
```

---

#### `glyph check [name...]`

Type-check without codegen. If names given, check only those definitions
and their transitive dependencies.

```
$ glyph check
OK 47 defs, 0 errors

$ glyph check process transform
OK process : [Item] -> [Item]
OK transform : Item -> Item
```

---

#### `glyph test [pattern]`

Run test definitions. Optional glob pattern.

```
$ glyph test
PASS test_parse_csv (2ms)
PASS test_search (5ms)
FAIL test_edge_case (1ms)
  expected: 42
  got:      41
2/3 passed

$ glyph test "*csv*"
PASS test_parse_csv (2ms)
1/1 passed
```

---

#### `glyph repl`

Interactive evaluation loop. Definitions persist to the DB.

```
$ glyph repl
glyph> 1 + 2
3
glyph> double x = x * 2
fn double : I -> I  (saved)
glyph> double 21
42
glyph> :type double
I -> I
glyph> :undo
removed double
```

REPL meta-commands start with `:` — `:type`, `:undo`, `:quit`, `:load`.

---

### 2.2 QUERY & NAVIGATE

#### `glyph get <name> [--kind <kind>] [--sig] [--meta]`

Retrieve a definition by name. The fundamental read operation.

```
$ glyph get process
fn process items = items |> filter .valid |> map transform |> collect

$ glyph get Item
type Item = {id:U name:S valid:B data:[U]}

$ glyph get process --sig
process : [Item] -> [Item]

$ glyph get process --meta
fn process : [Item] -> [Item]
  tokens: 12
  hash: a3f8...
  deps: filter, map, transform, collect
  rdeps: main
  tags: pure
  modified: 2025-02-20T14:30:00
```

If ambiguous (same name, different kinds), all are returned:

```
$ glyph get Point
type Point = {x:F y:F}
fn Point x:F y:F = {x y}       -- constructor
```

Use `--kind` to disambiguate: `glyph get Point --kind type`.

Exit code: 1 if not found.

---

#### `glyph ls [--kind <kind>] [--module <mod>] [--tag <key>[=<val>]] [--sort <field>]`

List definitions. The "directory listing" of the image.

```
$ glyph ls
fn   main           [S] -> !V
fn   process        [Item] -> [Item]
fn   transform      Item -> Item
fn   search         S -> S -> ![Match]
type Item           {id:U name:S valid:B data:[U]}
type Match          {path:S line:I text:S}
type CsvDoc         {header:[S] data:[[S]]}
test test_parse_csv
test test_search

$ glyph ls --kind fn
fn main           [S] -> !V
fn process        [Item] -> [Item]
fn transform      Item -> Item
fn search         S -> S -> ![Match]

$ glyph ls --kind fn --sort tokens
fn transform      Item -> Item                   (8 tok)
fn process        [Item] -> [Item]               (12 tok)
fn search         S -> S -> ![Match]             (28 tok)
fn main           [S] -> !V                      (42 tok)

$ glyph ls --tag pure
fn transform      Item -> Item
fn process        [Item] -> [Item]

$ glyph ls --module io
fn read           S -> !S
fn write          S -> S -> !V
fn say            S -> V
```

---

#### `glyph find <pattern> [--body] [--regex]`

Full-text search across names, signatures, and optionally bodies.

```
$ glyph find parse
fn   parse_csv      S -> !CsvDoc       -- name match
fn   parse_int      S -> ?I            -- name match

$ glyph find "filter" --body
fn   process        [Item] -> [Item]   -- body contains 'filter'
fn   search         S -> S -> ![Match] -- body contains 'filter'

$ glyph find "->.*\[" --regex
fn   process        [Item] -> [Item]   -- sig matches
fn   search         S -> S -> ![Match] -- sig matches
```

This replaces grep. The database makes this instantaneous.

---

#### `glyph deps <name> [--depth <n>] [--tree] [--inverse]`

Show dependency graph from a definition.

```
$ glyph deps main
main -> process, search, say

$ glyph deps main --tree
main
├── process
│   ├── filter
│   ├── map
│   └── transform
│       └── validate  (extern)
├── search
│   ├── read
│   ├── split
│   ├── filter
│   └── has
└── say

$ glyph deps main --tree --depth 1
main
├── process
├── search
└── say
```

---

#### `glyph rdeps <name>`

Reverse dependencies: "who uses this?"

```
$ glyph rdeps transform
fn process        [Item] -> [Item]

$ glyph rdeps Item
fn process        [Item] -> [Item]
fn transform      Item -> Item
fn main           [S] -> !V
type Match        uses Item (via field_of)
```

This is the "what breaks if I change this?" query.

---

#### `glyph diff [name] [--from <snapshot>]`

Show what changed since last build, or diff a definition against a snapshot.

```
$ glyph diff
M  fn process          (body changed, +3 tokens)
A  fn validate         (new)
D  fn old_transform    (removed)

$ glyph diff process
 process items =
   items
     |> filter .valid
-    |> map transform
+    |> par map transform
     |> collect
```

---

#### `glyph stat`

Image statistics. Useful for LLM budget planning.

```
$ glyph stat
program.glyph
  defs:     47 (38 fn, 5 type, 2 test, 1 fsm, 1 srv)
  externs:  3
  tokens:   842 total (avg 17.9/def, max 65 in main)
  compiled: 44/47 (3 dirty)
  deps:     89 edges
  size:     256 KB
  stdlib:   attached (312 defs)
```

---

#### `glyph dump [name...] [--all] [--budget <tokens>]`

Dump definitions as plain text. This is the primary "give me context" command
for LLMs. Token-budget-aware.

```
$ glyph dump --all
-- types
Item = {id:U name:S valid:B data:[U]}
Match = {path:S line:I text:S}
CsvDoc = {header:[S] data:[[S]]}

-- functions
process items = items |> filter .valid |> map transform |> collect
transform item = ...
search pat path = ...
main = ...

-- externs
extern "libc"
  malloc : U -> *V
  free : *V -> V

$ glyph dump --budget 200
[200 token budget: 11/47 defs included, prioritized by dependency depth from main]
Item = {id:U name:S valid:B data:[U]}
transform item = ...
process items = ...
main = ...
```

The `--budget` flag is the killer feature for LLM context loading. It does a
dependency-ordered traversal from `main` (or a specified root) and includes
as many definitions as fit within the token budget. Signatures are included
for definitions that didn't fit.

```
$ glyph dump --budget 100 --root process
[100 token budget: 4/47 defs, root=process]
Item = {id:U name:S valid:B data:[U]}
transform item = validate item! |> {item with score: compute item.data}
process items = items |> filter .valid |> map transform |> collect
-- also referenced (sigs only, over budget):
--   validate : Item -> ?Item
--   compute : [U] -> F
```

---

#### `glyph cat <name>`

Like `get`, but outputs *only* the body with no prefix or metadata.
Designed for piping into other commands or back into `glyph put`.

```
$ glyph cat process
process items = items |> filter .valid |> map transform |> collect
```

Useful for: `glyph cat process | sed 's/map/par map/' | glyph put fn process`

---

### 2.3 MODIFY

#### `glyph put <kind> <name> [--sig <sig>] [--tag <k>=<v>]... < body`

Create or update a definition. The fundamental write operation.
Body is read from stdin, a heredoc, or inline after `=`.

```
-- Inline (short definitions)
$ glyph put fn double = "double x = x * 2"

-- Heredoc (multi-line)
$ glyph put fn process <<'EOF'
process items =
  items
    |> filter .valid
    |> par map transform
    |> collect
EOF

-- Pipe
$ echo 'Item = {id:U name:S valid:B data:[U]}' | glyph put type Item

-- With explicit signature
$ glyph put fn process --sig "[Item] -> [Item]" = "process items = ..."

-- With tags
$ glyph put fn process --tag pure --tag complexity=O(n) <<'EOF'
...
EOF
```

Output:
```
put fn process (12 tokens, hash a3f8...)

-- Or on update:
put fn process (updated, 12->15 tokens, 2 rdeps dirty)
```

**Idempotency**: if the body hasn't changed (same hash), this is a no-op
and prints `put fn process (unchanged)`.

---

#### `glyph edit <name> [--kind <kind>]`

Open a definition in `$EDITOR` for interactive editing. On save, the
definition is updated in the DB. For LLMs, this is less relevant than
`put`, but essential for human fallback.

```
$ glyph edit process          # opens $EDITOR with body
$ EDITOR=vim glyph edit main  # explicit editor
```

---

#### `glyph rm <name> [--kind <kind>] [--force]`

Remove a definition.

```
$ glyph rm old_transform
removed fn old_transform

$ glyph rm Item
ERR Item has 3 reverse dependencies: process, transform, main
    use --force to remove anyway (will cause compile errors)

$ glyph rm Item --force
removed type Item (3 defs now have broken references)
```

---

#### `glyph mv <old_name> <new_name>`

Rename a definition and update all references.

```
$ glyph mv transform xform
renamed fn transform -> xform
updated 2 references (process, main)
```

---

#### `glyph tag <name> <key>[=<value>]`
#### `glyph tag <name> --rm <key>`

Manage tags on definitions.

```
$ glyph tag process pure
tagged process: pure

$ glyph tag process complexity=O(n)
tagged process: complexity=O(n)

$ glyph tag process --rm pure
untagged process: pure

$ glyph get process --meta | grep tags
  tags: complexity=O(n)
```

---

#### `glyph extern <name> <symbol> <sig> [--lib <lib>] [--conv <conv>]`

Declare a foreign function. Shorthand for the extern block syntax.

```
$ glyph extern clock clock_gettime "I -> *V -> I" --lib librt
extern clock : I -> *V -> I (symbol: clock_gettime, lib: librt)

$ glyph extern malloc malloc "U -> *V"
extern malloc : U -> *V (symbol: malloc, lib: libc)

-- List all externs
$ glyph ls --kind extern
extern clock          I -> *V -> I     (librt:clock_gettime)
extern malloc         U -> *V          (libc:malloc)
extern free           *V -> V          (libc:free)
```

---

### 2.4 ADVANCED

#### `glyph sql <query>`

Escape hatch: run raw SQL against the program database. For power users
and edge cases the CLI doesn't cover.

```
$ glyph sql "SELECT name, tokens FROM def ORDER BY tokens DESC LIMIT 5"
main            65
search          28
parse_csv       24
process         15
transform       8

$ glyph sql "SELECT COUNT(*) FROM dep WHERE edge = 'calls'"
89
```

---

#### `glyph attach <path> [--as <alias>]`

Attach a library database. Definitions become available for import.

```
$ glyph attach ./http.glyph --as http
attached http.glyph as 'http' (24 defs)

$ glyph ls --module http
fn http.get       S -> !Response
fn http.post      S -> S -> !Response
type http.Response {status:I body:S headers:{S:S}}
```

---

#### `glyph snapshot [name]`

Create a named snapshot (savepoint) of the current image state.

```
$ glyph snapshot before-refactor
snapshot 'before-refactor' created (47 defs, 842 tokens)

$ glyph snapshot
snapshot 'auto-20250220-143000' created
```

Under the hood this copies the database (or uses SQLite's backup API).

---

#### `glyph restore <name>`

Restore from a snapshot.

```
$ glyph restore before-refactor
restored from 'before-refactor' (47 defs, 842 tokens)
warning: 3 defs created after snapshot will be lost
```

---

#### `glyph migrate`

Upgrade the database schema when the Glyph version changes.

```
$ glyph migrate
migrating program.glyph: schema v0.1 -> v0.2
  added column def.visibility
  added table effect
done
```

---

## 3. Batch Operations

Multiple commands can be piped as a batch for atomic execution.
This is the primary mode for LLMs doing multi-step edits.

```bash
glyph batch <<'BATCH'
put type Config = "Config = {host:S port:I db:S}"
put fn load_config = "load_config path:S = read(path)? |> parse_toml |> into Config"
put fn main = "main = cfg = load_config \"app.toml\"! ; serve cfg"
tag load_config io
check
BATCH
```

Output:
```
put type Config (8 tokens)
put fn load_config (18 tokens)
put fn main (updated, 42->38 tokens)
tagged load_config: io
OK 49 defs, 0 errors
--- batch: 5 commands, 0 failures ---
```

If any command fails in batch mode, subsequent commands still execute
(like `make -k`), and the exit code reflects the worst failure.
Use `glyph batch --strict` to abort on first error with rollback.

### 3.1 Batch from File

```bash
$ glyph batch < changes.glyph-batch
```

A `.glyph-batch` file is just one command per line (without the `glyph` prefix).

---

## 4. LLM-Optimized Workflow

This section describes the expected interaction pattern when an LLM
(like Claude Code) is the primary developer.

### 4.1 Context Loading

The LLM starts by understanding the program:

```bash
# Quick overview
glyph stat

# What exists?
glyph ls

# Load as much as fits in context
glyph dump --budget 4000

# Or focus on a specific area
glyph dump --budget 2000 --root handle_request
```

### 4.2 Targeted Reading

When working on a specific definition:

```bash
# Read the function
glyph get handle_request

# What does it depend on?
glyph deps handle_request --tree

# Read a specific dependency
glyph get parse_body

# Search for related code
glyph find "validate" --body
```

### 4.3 Writing Code

```bash
# Create or update
glyph put fn handle_request <<'EOF'
handle_request req:Request =
  body = parse_body req ?
  validated = validate body ?
  result = process validated ?
  {status: 200 body: to_json result}
EOF

# Quick check
glyph check handle_request

# Run tests
glyph test "*request*"
```

### 4.4 Refactoring

```bash
# What depends on the thing I want to change?
glyph rdeps OldType

# Rename
glyph mv OldType NewType

# Check nothing broke
glyph check

# See what changed
glyph diff
```

### 4.5 Complete Workflow Example

LLM task: "Add caching to the fetch function"

```bash
# 1. Understand current state
glyph get fetch --meta
glyph deps fetch --tree

# 2. Add cache type
glyph put type Cache = "Cache[K,V] = {store:{K:V} ttl:I max:I}"

# 3. Add cache functions
glyph put fn cache_new = "cache_new ttl:I max:I = {store:{} ttl max}"
glyph put fn cache_get = "cache_get c:&Cache k = c.store[k]"
glyph put fn cache_set = "cache_set c:Cache k v = {c with store: c.store.set k v}"

# 4. Update fetch to use cache
glyph put fn fetch <<'EOF'
fetch url:S cache:Cache =
  match cache_get cache url
    Some hit -> hit
    None ->
      r = http_get(url)?
      cache_set cache url r
      r
EOF

# 5. Verify
glyph check
glyph test
glyph diff
```

Total LLM tokens spent on CLI commands: ~120.
Equivalent SQL workflow: ~400+ tokens.

---

## 5. Machine-Readable Output Modes

### 5.1 JSON Mode (`--json` / `-j`)

Every read command supports `--json`:

```bash
$ glyph get process -j
{"name":"process","kind":"fn","sig":"[Item] -> [Item]","body":"process items = items |> filter .valid |> map transform |> collect","tokens":12,"hash":"a3f8...","compiled":true}

$ glyph ls -j
[{"name":"main","kind":"fn","sig":"[S] -> !V","tokens":42},{"name":"process","kind":"fn","sig":"[Item] -> [Item]","tokens":12},...]

$ glyph deps main -j
{"name":"main","deps":[{"name":"process","edge":"calls"},{"name":"search","edge":"calls"},{"name":"say","edge":"calls"}]}
```

### 5.2 TSV Mode (`--tsv`)

Tab-separated, for piping to `awk`/`cut`:

```bash
$ glyph ls --tsv
fn	main	[S] -> !V	42
fn	process	[Item] -> [Item]	12
...
```

### 5.3 Quiet Mode (`--quiet` / `-q`)

Only output what's strictly necessary. For LLM workflows where
confirmation noise wastes tokens:

```bash
$ glyph put fn double = "double x = x * 2" -q
# (no output on success)
$ echo $?
0

$ glyph check -q
# (no output if all OK)
$ echo $?
0

$ glyph check -q
ERR process:4 type mismatch
$ echo $?
2
```

---

## 6. Exit Code Reference

| Code | Meaning            | Example                           |
|------|--------------------|-----------------------------------|
| 0    | Success            | Build complete, check passed      |
| 1    | Not found          | `glyph get nonexistent`           |
| 2    | Type error         | Failed type check                 |
| 3    | Compile error      | Syntax error, unresolved name     |
| 4    | Runtime error      | Panic during `glyph run`          |
| 5    | Database error     | Corrupt image, schema mismatch    |
| 6    | Usage error        | Bad arguments, unknown command     |
| 7    | Batch partial fail | Some commands in batch failed     |

---

## 7. Environment Variables

| Variable      | Purpose                              | Default           |
|---------------|--------------------------------------|--------------------|
| `GLYPH_DB`   | Default database path                | `./program.glyph`  |
| `GLYPH_STDLIB`| Path to stdlib database             | `/usr/lib/glyph/stdlib.glyph` |
| `GLYPH_TARGET`| Default compilation target          | host triple        |
| `GLYPH_BUDGET`| Default token budget for `dump`     | 4000               |
| `GLYPH_COLOR` | Color mode                          | auto               |
| `EDITOR`      | Editor for `glyph edit`             | vi                 |

---

## 8. Comparison: LLM Token Cost for Common Tasks

| Task                        | Glyph CLI    | File-based + SQL | Raw SQL     |
|-----------------------------|:------------:|:----------------:|:-----------:|
| Read a function             | 3 tok        | 8 tok            | 18 tok      |
| List all types              | 4 tok        | 6 tok            | 15 tok      |
| Write a function            | 8-15 tok     | 20-30 tok        | 35-50 tok   |
| Find usage of name          | 4 tok        | 8 tok            | 22 tok      |
| Full context load (budgeted)| 5 tok        | N/A              | 40+ tok     |
| Rename + update refs        | 3 tok        | manual           | 30+ tok     |
| Check types                 | 2 tok        | 3 tok            | N/A         |
| Batch: add 3 defs + check   | 25 tok       | 45 tok           | 80+ tok     |

---

## 9. Shell Completions & Discoverability

```bash
# Completions are definition-aware:
$ glyph get pr<TAB>
process    parse_csv    parse_int

$ glyph deps <TAB>
main  process  search  transform  validate  parse_csv ...

# Help is terse:
$ glyph help
init          Create new program image
build         Compile dirty definitions
run           Build and execute main
check         Type-check (no codegen)
test          Run test definitions
repl          Interactive evaluation
get           Retrieve definition by name
ls            List definitions
find          Search names and bodies
deps/rdeps    Show dependency graph
diff          Show changes since last build
stat          Image statistics
dump          Export definitions (token-budgeted)
cat           Raw body output (for piping)
put           Create or update definition
edit          Open in $EDITOR
rm            Remove definition
mv            Rename + update references
tag           Manage definition tags
extern        Declare foreign function
batch         Run multiple commands atomically
sql           Raw SQL escape hatch
attach        Link library database
snapshot      Save image state
restore       Restore from snapshot
```

---

## 10. Design Decisions & Rationale

**Why `put` not `add`/`update`?**
An upsert is the natural operation. The LLM doesn't care whether a
definition exists yet — it has the code, it wants it in the image.
`put` is idempotent and always correct.

**Why `get`/`put`/`rm` not `read`/`write`/`delete`?**
3 characters vs 4-6. At scale across thousands of LLM interactions,
this adds up. These are also the most natural HTTP-verb-adjacent
terms (consistent with REST intuition in training data).

**Why `dump --budget` not `context`?**
`dump` is a standard Unix concept. Adding `--budget` to it is more
composable than a single-purpose command. But we could alias
`glyph context 2000` → `glyph dump --budget 2000` if usage patterns
show LLMs prefer it.

**Why not subcommands like `glyph def get` / `glyph def put`?**
Extra token per command. Definitions are the primary object, so they
get top-level verbs. If we add non-definition objects later (build
configs, profiles), they can use a namespace: `glyph config set`.

**Why heredoc for multi-line?**
The `<<'EOF'` pattern is 3 tokens and universally understood by LLMs
from training data. The quoting prevents interpolation issues.
Alternative: read from stdin via pipe (`echo "..." | glyph put fn x`).
Both work; heredoc is more idiomatic for multi-line.

**Why not a REPL-only model?**
The batch-command model is better for LLMs because: (a) each command
is independently retry-able on failure, (b) commands can be generated
in parallel, (c) the LLM can plan a sequence before executing, and
(d) it avoids stateful session management.
