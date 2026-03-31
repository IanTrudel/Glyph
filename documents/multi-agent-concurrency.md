# Multi-Agent Concurrent Development

## Overview

Glyph's database-as-program model uniquely enables multiple agents (LLMs, humans, or mixed) to work on the same `.glyph` file simultaneously with definition-level conflict semantics. This document analyzes the current architecture's readiness, identifies the concrete gaps, and proposes a phased implementation.

## Why Glyph Is Already 90% There

The existing architecture provides natural foundations for concurrency:

- **WAL mode** (already enabled): unlimited concurrent readers, readers never block writers
- **Snapshot isolation**: each connection sees a consistent state, no dirty reads
- **Single writer serialization**: writes queue automatically via SQLite — the write lock is held for microseconds per MCP tool call, so dozens of agents can write interleaved without meaningful contention
- **Definition-level granularity**: the unit of work is a row in the `def` table, not a line in a file
- **Dependency graph** (`dep` table, `v_callgraph` view): the compiler already knows which definitions depend on which
- **Change history** (`def_history` table): triggers track every modification
- **Stateless MCP server**: no session state between tool calls — each request opens a fresh SQLite connection, queries/writes, and closes it

## SQLite Concurrency Model

### What WAL mode provides

| Aspect | Behavior |
|--------|----------|
| Concurrent readers | Unlimited, never block |
| Concurrent writers | One at a time (serialized via write lock) |
| Writers vs readers | Writers don't block readers, readers don't block writers |
| Lock granularity | Database-level (not row or table) |
| Isolation | Serializable snapshot isolation |
| Conflict signal | `SQLITE_BUSY` (immediate in WAL mode, retryable via `busy_timeout`) |

### Transaction types for concurrent use

| Type | Behavior | When to use |
|------|----------|-------------|
| `BEGIN DEFERRED` | No lock until first write | Read-only transactions |
| `BEGIN IMMEDIATE` | Acquires write lock at start, fails fast if unavailable | All write transactions |
| `BEGIN EXCLUSIVE` | Same as IMMEDIATE in WAL mode | Not needed |

### Key constraints

- **Single writer is a hard limit** — but MCP tool calls are short (single INSERT/UPDATE), so the lock is held briefly
- **Long read transactions block WAL checkpointing** — keep reads short, re-snapshot periodically
- **Never use SQLite over NFS/network filesystems** — POSIX locking and mmap are unreliable
- **`PRAGMA journal_mode=WAL` is persistent** — set once, all connections use it automatically
- **`PRAGMA busy_timeout = 5000`** — built-in retry with backoff, should be set by each connection

### Experimental features (not in mainline SQLite)

- **`BEGIN CONCURRENT`** (libsql/Turso fork): allows multiple writers on non-overlapping pages — would be ideal for definition-level concurrency but is not in standard SQLite
- **WAL2** (experimental branch): alternating WAL files to reduce checkpoint stalls — not merged

## The Core Problem: LLMs Don't Cooperate by Default

An LLM agent given a task will call `put_def` and overwrite whatever is there. It doesn't check if another session modified the definition 10 seconds ago. It doesn't know another agent exists. This isn't a theoretical concern — concurrent Claude Code sessions on the same `.glyph` file have already produced silent data loss where one session's changes were overwritten by another.

The root cause is that the MCP tools have zero concurrency awareness:

- **`get_def` doesn't return the hash.** The response includes name, kind, body, gen, tokens — but not the BLAKE3 hash that's already stored in the `def` table. The LLM has no concurrency token to work with even if it wanted to.
- **`put_def` is a blind overwrite.** It accepts name+body and writes unconditionally. No check, no warning, no option to detect conflicts.
- **No enforcement is possible at the LLM level.** Even if tool documentation says "check for conflicts", LLMs cut corners under task pressure. Soft instructions won't prevent collisions.

**The fix must be server-side — the MCP tool itself must refuse unsafe writes.** The LLM shouldn't need to opt into safety; it should be unable to opt out.

### The compare-and-swap pattern

The `def` table already has a `hash` column (BLAKE3 of kind+sig+body). This is the concurrency token. The fix:

1. **`get_def` returns `hash`** in its response: `{"name":"foo", "body":"...", "hash":"74FCFA...", ...}`
2. **`put_def` accepts `base_hash`** parameter
3. **The write uses compare-and-swap:** `UPDATE def SET body=? WHERE name=? AND hash=?` — if the hash changed since the agent last read, the UPDATE affects 0 rows, and the tool returns a conflict error
4. **The conflict error is LLM-readable:** "CONFLICT: 'foo' was modified since your last read (changed at 2026-03-31 03:15:00). Your base_hash 74FCFA... does not match current hash A1B2C3... Call get_def('foo') to see the current version before modifying."

This works across multiple MCP server processes because the check happens in the database, not in server memory. Two `glyph mcp` processes against the same `.glyph` file both hit the same SQLite rows.

### Enforcement levels

| Level | Mechanism | Prevents collisions? |
|-------|-----------|---------------------|
| **Soft** | Skill docs say "use base_hash" | No — LLMs ignore optional guidance |
| **Medium** | `put_def` without `base_hash` returns a warning | Partial — warning visible but doesn't block |
| **Hard (recommended)** | `put_def` requires `base_hash` for existing defs | Yes — no way to bypass |

The hard approach: if the definition already exists and `base_hash` is not provided, reject with "base_hash required when updating existing definitions — call get_def first." New definitions (INSERT) don't need `base_hash`. This forces the read-before-write pattern that makes conflict detection automatic.

### What the LLM experiences

**Before (current, unsafe):**
```
LLM: get_def(name="foo")           → body returned
LLM: [modifies body]
LLM: put_def(name="foo", body=new) → "ok" (silently overwrites other agent's changes)
```

**After (with compare-and-swap):**
```
LLM: get_def(name="foo")           → body + hash "74FCFA..." returned
LLM: [modifies body]
LLM: put_def(name="foo", body=new, base_hash="74FCFA...") → "ok"
```

**Collision detected:**
```
LLM A: get_def(name="foo")         → hash "74FCFA..."
LLM B: get_def(name="foo")         → hash "74FCFA..."
LLM B: put_def(name="foo", body=B_version, base_hash="74FCFA...") → "ok", hash is now "A1B2C3..."
LLM A: put_def(name="foo", body=A_version, base_hash="74FCFA...") → CONFLICT: hash changed to "A1B2C3..."
LLM A: get_def(name="foo")         → sees B's changes, hash "A1B2C3..."
LLM A: [incorporates B's changes, re-applies own changes]
LLM A: put_def(name="foo", body=merged, base_hash="A1B2C3...") → "ok"
```

## What Else Is Broken for Concurrent Use

### 1. No transactions on writes

Read-modify-write sequences in `mcp_do_put` aren't wrapped in `BEGIN IMMEDIATE ... COMMIT`. The race window is small but real. The compare-and-swap UPDATE should be in a transaction.

### 2. Hardcoded temp file paths

Build and run tools write to:
- `/tmp/glyph_mcp_build.txt` (build log)
- `/tmp/glyph_mcp_run_bin` (compiled binary)
- `/tmp/glyph_mcp_stdin.txt` (run input)

Two concurrent builds clobber each other's output.

### 3. No change attribution

`def_history` records what changed and when, but not who changed it. No session or agent identity.

### 4. No conflict surface

There's no way for agent B to know that agent A is working on a definition or its dependents. No claims, no locks, no visibility.

## The Protocol: Three Layers

### Layer 1: Compare-and-swap (required, ~8-10 definitions)

**Modify `get_def` / `get_defs` to return hash:**

Add `hash` (hex string) to the JSON response. The hash is already in the `def` table — just include it in the SELECT and response formatting.

```json
{"name":"foo", "kind":"fn", "body":"foo x = x + 1", "hash":"74FCFAC064EB...", "gen":1, "tokens":8}
```

**Modify `put_def` / `put_defs` to accept and enforce `base_hash`:**

The core mechanism — compare-and-swap at the SQL level:

```sql
BEGIN IMMEDIATE;
-- For updates: compare-and-swap
UPDATE def SET body=?, hash=?, tokens=?
WHERE name=? AND kind=? AND gen=? AND hash=X'base_hash_here';
-- If affected rows == 0 and def exists, it's a conflict
-- If def doesn't exist, INSERT as usual (no base_hash needed)
COMMIT;
```

When the UPDATE affects 0 rows but the definition exists, return:

```json
{"error": "CONFLICT",
 "message": "Definition 'foo' was modified since your last read. Call get_def('foo') to see the current version.",
 "current_hash": "A1B2C3...",
 "your_base_hash": "74FCFA...",
 "changed_at": "2026-03-31T03:15:00"}
```

**Enforcement:** `put_def` on an existing definition without `base_hash` is rejected: "base_hash required when updating existing definitions — call get_def first." New definitions (no existing row) don't require it.

**Session-specific temp paths:**

Use PID or a random suffix for build/run temp files:

```
/tmp/glyph_mcp_build_{pid}.txt
/tmp/glyph_mcp_run_{pid}
```

Trivial change, eliminates build/run collisions entirely.

### Layer 2: Coordination Visibility (~1 session)

**New schema:**

```sql
CREATE TABLE session (
    id TEXT PRIMARY KEY,
    agent_name TEXT,
    started_at TEXT NOT NULL DEFAULT (datetime('now')),
    heartbeat TEXT NOT NULL DEFAULT (datetime('now')),
    status TEXT NOT NULL DEFAULT 'active'
        CHECK(status IN ('active', 'done', 'stale'))
);

CREATE TABLE claim (
    session_id TEXT NOT NULL REFERENCES session(id) ON DELETE CASCADE,
    def_name TEXT NOT NULL,
    kind TEXT NOT NULL DEFAULT 'fn',
    claimed_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (def_name, kind)
);
```

**Add `session_id` to `def_history`:**

```sql
ALTER TABLE def_history ADD COLUMN session_id TEXT;
```

This traces who made each change. Combined with the session table, you get full audit trails.

**New MCP tools:**

| Tool | Purpose |
|------|---------|
| `register_session` | Create session, get session_id |
| `heartbeat` | Update last-seen timestamp |
| `claim_defs` | Claim definitions for modification (advisory) |
| `release_defs` | Release claims |
| `list_claims` | See what other agents are working on |

Claims are **advisory** — they don't block writes. They inform other agents. The `base_hash` mechanism (Layer 1) catches real conflicts regardless of claims.

**Stale session cleanup:**

A simple query identifies abandoned sessions:

```sql
UPDATE session SET status = 'stale'
WHERE status = 'active'
  AND julianday('now') - julianday(heartbeat) > 0.003;  -- ~5 minutes
DELETE FROM claim WHERE session_id IN (SELECT id FROM session WHERE status = 'stale');
```

### Layer 3: Orchestration (ongoing, lives outside the compiler)

The orchestrator is an MCP client that uses the same tools to plan and coordinate. It could be a Glyph program, a Claude Code session, or a purpose-built agent.

**Responsibilities:**

1. **Partition work** via the dependency graph
2. **Assign definitions** to agents with claims
3. **Monitor progress** via `def_history` and claims
4. **Detect semantic conflicts** at agent boundaries
5. **Coordinate builds** at checkpoints

## Empirical Analysis: glyph.glyph Dependency Graph

The following analysis is based on the actual dependency graph of the self-hosted compiler (1,399 fn definitions, 1,526 dependency edges, 12 major namespaces). This is not theoretical — these are real measurements of the codebase's structure.

### Namespace isolation scores

How much of each namespace's dependency edges are internal (within the same namespace)?

| Namespace | Fn count | Internal edges % | Interpretation |
|-----------|----------|-------------------|----------------|
| json | 56 | 100.0% | Completely self-contained |
| tokenizer | 84 | 98.6% | Nearly self-contained |
| mir | 100 | 97.6% | Nearly self-contained |
| typeck | 134 | 72.2% | Moderately coupled |
| codegen | 144 | 61.8% | Moderately coupled |
| mcp | 72 | 40.7% | Consumes other namespaces |
| lower | 74 | 28.7% | Bridge between parser and mir |
| parser | 126 | 25.3% | Heavily depends on tokenizer |
| llvm | 78 | 13.6% | Depends on mir and codegen |
| build | 42 | 6.3% | Orchestration, depends on everything |
| cli | 40 | 0.0% | Pure consumer |
| mk | 23 | 0.0% | Pure consumer (constructors) |

### Functions safe to modify independently

The critical metric: what percentage of each namespace's functions are called **only** by other functions in the same namespace? These are "internal-only" — an agent can freely change them with zero risk of affecting other namespaces.

| Namespace | Total | Internal-only | % Safe to modify freely |
|-----------|-------|---------------|-------------------------|
| mono | 38 | 38 | **100%** |
| json | 56 | 55 | **98.2%** |
| lower | 74 | 72 | **97.3%** |
| mcp | 72 | 70 | **97.2%** |
| build | 42 | 41 | **97.6%** |
| cli | 40 | 39 | **97.5%** |
| codegen | 144 | 137 | **95.1%** |
| llvm | 78 | 74 | **94.9%** |
| typeck | 134 | 97 | **72.4%** |
| parser | 126 | 88 | **69.8%** |
| mir | 100 | 46 | **46.0%** |
| tokenizer | 84 | 28 | **33.3%** |

**Key finding:** 8 of 12 namespaces have >94% of their functions safe to modify independently. An agent assigned to refactor `codegen` can change 137 out of 144 functions with zero conflict risk.

The two "infrastructure" namespaces — `mir` (46%) and `tokenizer` (33.3%) — export many constructor functions used everywhere. But these constructors are *stable constants* (they define MIR node kinds and token types). They essentially never change unless the language syntax or IR changes.

### The interface surface

241 functions out of 1,399 (17%) are called from outside their own namespace. These are the "interface" functions — the contracts between namespaces. They fall into three categories:

**1. Constructors / constants (majority, ~180 of 241).** MIR node constructors (`ok_local`, `rv_call`, `rv_field`, `tm_return`, etc.), parser AST constructors (`ex_ident`, `ex_call`, `pat_ident`, etc.), type constructors (`ty_int`, `ty_float`, `ty_array`, etc.). These define the shared vocabulary. They are effectively immutable — their signatures and semantics never change. An agent can treat these as frozen without negotiation.

**2. Core algorithms (~25).** Functions like `pool_get` (8 caller namespaces), `inst_type` (6), `subst_resolve` (5). These implement the type checker's core operations. Their signatures are stable but their *bodies* may change during optimization work. Safe to modify internally as long as the contract (parameters, return type, semantics) is preserved.

**3. Entry points (~35).** Functions like `tc_pre_register`, `tc_infer_loop_warm`, `tc_register_externs`, `check_parse_errors`. These are called by build/mcp/cli to invoke subsystems. Changing their signatures requires coordination with the callers.

### Three-group split analysis

A natural 3-group partition of the compiler pipeline:

| Group | Namespaces | Fn count | Tests |
|-------|------------|----------|-------|
| Frontend | tokenizer, parser | 210 | 57 |
| Middle | typeck, mono | 172 | 37 |
| Backend | codegen, llvm, lower, mir | 396 | 71 |
| Infra | build, cli, mcp, json, + others | ~620 | ~216 |

Cross-group dependency edges:

| From → To | Frontend | Middle | Backend | Infra |
|-----------|----------|--------|---------|-------|
| **Frontend** | 294 | 38 | 10 | 2 |
| **Middle** | 37 | 214 | **4** | 29 |
| **Backend** | 66 | **2** | 436 | 61 |
| **Infra** | 31 | 80 | 56 | 166 |

**The middle↔backend boundary has exactly 6 edges.** These are:

| From | To | Nature |
|------|----|--------|
| `typeck::is_op_float` | `mir::ok_const_float` | MIR constructor (stable) |
| `typeck::is_op_float` | `mir::ok_local` | MIR constructor (stable) |
| `typeck::infer_stmt` | `mir::st_expr` | MIR constructor (stable) |
| `typeck::infer_stmt` | `mir::st_let` | MIR constructor (stable) |
| `lower::lower_binary` | `typeck::tctx_is_float_bin` | Type context query (narrow) |
| `lower::lower_binary` | `typeck::tctx_is_str_bin` | Type context query (narrow) |

4 of the 6 are calls to immutable constructors. The type checker and the backend are essentially independent systems connected by 6 thin wires.

The backend→frontend boundary (66 edges) is **entirely** AST/token constructors: `ex_ident`, `ex_call`, `ex_array`, `pat_ident`, `op_type`, etc. These define the AST node vocabulary and never change unless the language grammar changes.

**Conclusion:** Three agents working on frontend, middle, and backend would have near-zero behavioral coupling. The cross-group interfaces are constructors that both sides treat as frozen constants.

### Test partitioning

Tests partition cleanly by namespace (by name prefix):

| Test area | Count |
|-----------|-------|
| lower/mir | 31 |
| typeck | 30 |
| parser | 29 |
| tokenizer | 28 |
| codegen | 24 |
| llvm | 8 |
| mono | 7 |
| json | 7 |
| mcp | 6 |
| exhaust | 4 |
| integration (cross-cutting) | 207 |

Each agent runs its namespace's tests during development. The 207 integration tests (named `test_*` without a namespace prefix) run **after all agents complete** as a final validation pass. Note: tests have no entries in the `dep` table — partitioning is by name convention.

## Work Partitioning via the Dependency Graph

The `dep` table contains everything needed. Given a set of definitions to modify:

```sql
-- Find all definitions in a namespace
SELECT id, name FROM def WHERE ns = 'typeck' AND kind = 'fn';

-- Find their internal dependencies
SELECT d.from_id, d.to_id, d.edge
FROM dep d
JOIN def f ON d.from_id = f.id
JOIN def t ON d.to_id = t.id
WHERE f.ns = 'typeck' AND t.ns = 'typeck';
```

**Partition algorithm:**

1. Build the induced subgraph of the target definitions
2. Find connected components — these are independent work units
3. Assign each component to a different agent
4. Cross-component edges are read-only dependencies (safe for concurrent access)

For the negative-literal migration (179 definitions), the dependency graph would likely partition into 20-30 independent clusters, each assignable to a separate agent.

### The interface freeze protocol

During multi-agent work, the orchestrator identifies interface functions and assigns them a freeze level:

| Freeze level | Rule | Examples |
|--------------|------|----------|
| **Hard freeze** | Cannot be modified by any agent | Constructors: `ok_local`, `rv_call`, `ex_ident`, `ty_int` |
| **Body-only** | Body can change, signature must not | Core algorithms: `pool_get`, `inst_type`, `subst_resolve` |
| **Coordinated** | Changes require orchestrator approval | Entry points: `tc_pre_register`, `check_parse_errors` |
| **Free** | Any agent with a claim can modify | Internal-only functions (72-100% of most namespaces) |

The orchestrator enforces this by:
1. Querying the interface surface at task start (the SQL queries above)
2. Assigning freeze levels based on caller count and function category
3. Including freeze levels in agent work assignments
4. Validating at merge time that no frozen signatures changed

### Worked example: "optimize the type checker"

Task: implement items 19.1 (type levels) and 19.2 (hash env lookup) from next-steps.md.

**Step 1 — Orchestrator analyzes the target:**
- Namespace: typeck (134 fns)
- 97 internal-only functions (72.4% free to modify)
- 37 interface functions (categorized above)
- Key functions to modify: `mk_engine`, `subst_fresh_var`, `subst_bind`, `generalize`, `env_insert`, `env_lookup`, `env_nullify`, `tc_infer_loop_warm`

**Step 2 — Orchestrator checks if the task is parallelizable:**
- Items 19.1 and 19.2 both modify `mk_engine` (adding fields to the engine record) — **conflict**
- They must be sequenced, not parallelized: 19.2 first (lower risk), then 19.1

**Step 3 — Orchestrator checks cross-namespace impact:**
- `tc_infer_loop_warm` is an entry point called by build/mcp/cli/emit (4 namespaces)
- Does 19.1 change its signature? No — it changes internal behavior only
- Does 19.2 change its signature? No — `env_lookup` returns the same type, just faster
- Verdict: no cross-namespace coordination needed. All changes are body-only on interface functions.

**Step 4 — Orchestrator assigns work:**
- Agent A: implement 19.2 (hash env, ~5-6 defs modified, low risk)
- Agent A runs typeck tests (30 tests) to validate
- Agent B: implement 19.1 (type levels, ~15 defs, high risk) — starts after A completes
- Agent B runs typeck tests + integration tests

**Step 5 — Alternatively, if the task WERE parallelizable:**
- Agent A gets 19.2 (env functions: `env_insert`, `env_lookup`, `env_nullify`, `env_push`, `env_pop`)
- Agent B gets a different typeck task that doesn't touch env functions
- Both claim their definitions, work concurrently, run namespace tests
- Orchestrator runs integration tests after both complete

## Two Models: Direct Editing vs. Branch-and-Merge

### Model A: Direct concurrent editing (real-time)

This is what the previous sections describe — multiple agents writing to the same `.glyph` database with compare-and-swap conflict detection. Fast feedback, but requires the concurrency machinery (base_hash, transactions, claims).

### Model B: Branch-and-merge (Squeak Trunk-style)

Squeak Smalltalk solved a similar problem for image-based development. Squeak's Trunk model: developers work on their own image copy, submit changesets (Monticello packages) to an inbox, trusted committers review and accept/reject, and the trunk image is rebuilt from accepted changes. Nobody edits the trunk directly.

Glyph can adopt this pattern because `.glyph` files are just SQLite databases — trivially copied:

```bash
cp program.glyph agent_a.glyph    # agent A gets a working copy
cp program.glyph agent_b.glyph    # agent B gets a working copy
```

Each agent works on their own copy with zero concurrency concerns. When done, a **diff-and-merge** step applies changes back:

```bash
glyph diff program.glyph agent_a.glyph    # show added/modified/deleted definitions
glyph merge program.glyph agent_a.glyph   # apply changes with conflict detection
```

The diff is at the definition level, not the line level. A definition is either:
- **Unchanged** (hash matches) — skip
- **Added** (exists in agent's copy, not in trunk) — insert
- **Modified** (exists in both, hash differs) — update, or conflict if trunk also changed
- **Deleted** (exists in trunk, not in agent's copy) — remove

**Advantages over direct editing:**
- Zero concurrency machinery needed — each agent has sole access to their file
- Works across machines (no shared filesystem required — just copy the .glyph file)
- Natural review checkpoint: someone (human or orchestrator) inspects the diff before merging
- No SQLite WAL/locking concerns
- The branch database is a full working compiler — agent can build and test independently

**Advantages of direct editing over branch-and-merge:**
- Real-time visibility — agents see each other's changes immediately
- No merge step — changes are live as soon as written
- Simpler workflow for quick coordinated edits

**The Squeak parallel goes further.** Squeak's Monticello tracks method-level changes in packages, with dependency metadata. Glyph's `def_history` table already records every change. A `glyph changeset` command could export a set of changes as a portable unit:

```bash
glyph changeset agent_a.glyph --since "2026-03-31 03:00:00"
# → outputs: 5 definitions modified, 2 added, 1 removed
# → writes changeset to agent_a.changeset (or JSON)
```

And a review tool:

```bash
glyph review program.glyph agent_a.changeset
# → shows each change with context
# → highlights conflicts with trunk
# → optionally applies after confirmation
```

### Which model when?

| Scenario | Model |
|----------|-------|
| Two Claude Code sessions working ad-hoc on the same project | **A** (direct, with compare-and-swap) |
| Orchestrated multi-agent task with planned partitioning | **B** (branch copies, merge after) |
| Multiple humans/LLMs collaborating over days | **B** (branch copies, review before merge) |
| Quick pair-programming style collaboration | **A** (direct, real-time) |

Both models benefit from Layer 1 (compare-and-swap). Model A requires it for safety. Model B uses it as a secondary check during the merge step — even though agents work on separate copies, the merge into trunk should verify that trunk hasn't changed at the definitions being merged.

### Implementation priority

**Model A (Layer 1)** should come first — it's the safety net that prevents the collision problem the user already hit. It's ~8-10 definitions and immediately protects all concurrent use.

**Model B** (`glyph diff` / `glyph merge` / `glyph changeset`) is a larger feature but builds on the same hash-comparison infrastructure. The diff algorithm is straightforward SQL:

```sql
-- Definitions modified in agent's copy vs trunk
SELECT a.name, a.kind, 'modified' as status
FROM agent.def a JOIN trunk.def t
  ON a.name = t.name AND a.kind = t.kind AND a.gen = t.gen
WHERE a.hash != t.hash;

-- Definitions added in agent's copy
SELECT a.name, a.kind, 'added' as status
FROM agent.def a
WHERE NOT EXISTS (
  SELECT 1 FROM trunk.def t
  WHERE t.name = a.name AND t.kind = a.kind AND t.gen = a.gen
);

-- Definitions removed in agent's copy
SELECT t.name, t.kind, 'removed' as status
FROM trunk.def t
WHERE NOT EXISTS (
  SELECT 1 FROM agent.def a
  WHERE a.name = t.name AND a.kind = t.kind AND a.gen = t.gen
);
```

Using SQLite's `ATTACH` to compare databases in a single query — no temp files, no export step.

## Changeset System (`glyph changeset`)

Modeled after Squeak Smalltalk's Monticello — the unit of collaboration is the **changeset**, a portable description of definition-level changes with per-operation ancestry tracking for conflict detection.

### Changeset format

A changeset is a JSON document:

```json
{
  "version": 1,
  "base_db": "glyph.glyph",
  "author": "agent-A",
  "message": "Optimize type checker env lookup with hash map",
  "created_at": "2026-03-31T04:30:00",
  "operations": [
    {
      "op": "modify",
      "name": "env_lookup",
      "kind": "fn",
      "gen": 1,
      "base_hash": "74FCFAC064EB...",
      "body": "env_lookup eng name =\n  hm_get(eng.env_map, name)"
    },
    {
      "op": "add",
      "name": "env_map_init",
      "kind": "fn",
      "gen": 1,
      "body": "env_map_init = hm_new(64)"
    },
    {
      "op": "remove",
      "name": "env_lookup_at",
      "kind": "fn",
      "gen": 1,
      "base_hash": "C3D4E5F6..."
    }
  ]
}
```

**Key design choices:**

- **JSON, not SQLite** — LLMs read and produce JSON natively. MCP tools return JSON. Changesets are small enough that a text format works. A 50-definition changeset is ~10KB.
- **`base_hash` per operation, not per changeset** — a changeset may span hours of work. Individual definitions may have been read at different times. Per-operation `base_hash` gives precise conflict detection: "this specific definition was in this state when I started modifying it."
- **Three operation types**: `modify` (base_hash required — proves you read the current version), `add` (no base_hash — the definition didn't exist), `remove` (base_hash required — proves you saw what you're deleting).
- **Idempotent application** — applying the same changeset twice is safe. A `modify` checks base_hash and fails if the definition was already updated (hash changed). An `add` fails if the definition already exists. A `remove` fails if already gone.

### Creating a changeset

**From a branch copy (primary workflow):**

```bash
cp program.glyph work.glyph                    # agent gets working copy
# ... agent works on work.glyph via MCP or CLI ...
glyph changeset work.glyph --base program.glyph -m "Optimize env lookup"
```

Implementation uses SQLite `ATTACH` to compare both databases in one query:

```sql
ATTACH 'program.glyph' AS base;

-- Modified definitions (same name+kind+gen, different hash)
SELECT 'modify' as op, w.name, w.kind, w.gen, hex(b.hash) as base_hash, w.body
FROM main.def w
JOIN base.def b ON w.name = b.name AND w.kind = b.kind AND w.gen = b.gen
WHERE w.hash != b.hash;

-- Added definitions (in work, not in base)
SELECT 'add' as op, w.name, w.kind, w.gen, NULL as base_hash, w.body
FROM main.def w
WHERE NOT EXISTS (
  SELECT 1 FROM base.def b
  WHERE b.name = w.name AND b.kind = w.kind AND b.gen = w.gen
);

-- Removed definitions (in base, not in work)
SELECT 'remove' as op, b.name, b.kind, b.gen, hex(b.hash) as base_hash, NULL as body
FROM base.def b
WHERE NOT EXISTS (
  SELECT 1 FROM main.def w
  WHERE w.name = b.name AND w.kind = b.kind AND w.gen = b.gen
);

DETACH base;
```

No temp files. No export step. One query per operation type, directly on the databases.

**From history (single-database workflow):**

```bash
glyph changeset program.glyph --since "2026-03-31 03:00:00" -m "Today's work"
```

Uses `def_history` (2,346 entries in glyph.glyph, avg 2 revisions per definition). For each definition modified since the timestamp: the old hash (from history) becomes `base_hash`, the current body becomes the new body. For removed definitions: the history entry captures the last body before deletion.

### Reviewing a changeset

```bash
glyph review program.glyph changeset.json
```

Output:

```
Changeset: "Optimize env lookup" by agent-A (2026-03-31 04:30)
3 operations:

  MODIFY env_lookup (fn, gen=1)
    base: 74FCFA... → trunk: 74FCFA...  ✓ clean
    -  env_lookup eng name = env_lookup_at(eng, len(eng.env_names) - 1, name)
    +  env_lookup eng name = hm_get(eng.env_map, name)

  ADD env_map_init (fn, gen=1)
    8 tokens, no conflict

  REMOVE env_lookup_at (fn, gen=1)
    base: C3D4E5... → trunk: C3D4E5...  ✓ clean

Conflicts: 0 / 3 operations
Ready to apply.
```

When there's a conflict:

```
  MODIFY env_lookup (fn, gen=1)
    base: 74FCFA... → trunk: B2C3D4...  ✗ CONFLICT
    Modified in trunk since changeset was created.
    Changeset version:  env_lookup eng name = hm_get(eng.env_map, name)
    Trunk version:      env_lookup eng name = env_lookup_linear(eng, name)
```

The review shows both versions so the human or LLM can decide how to resolve.

### Applying a changeset

```bash
glyph apply program.glyph changeset.json              # apply all, stop on conflict
glyph apply program.glyph changeset.json --skip-conflicts  # apply clean ops, report conflicts
glyph apply program.glyph changeset.json --force       # overwrite conflicts (last-writer-wins)
```

Each operation is applied in a transaction:

```sql
BEGIN IMMEDIATE;
-- For "modify": compare-and-swap
UPDATE def SET body=?, hash=?, tokens=?
WHERE name=? AND kind=? AND gen=? AND hash=X'base_hash';
-- rows_affected == 0 → conflict (hash changed) or definition gone
COMMIT;
```

**Result per operation:**

```json
{"results": [
  {"op": "modify", "name": "env_lookup", "status": "applied"},
  {"op": "add", "name": "env_map_init", "status": "applied"},
  {"op": "remove", "name": "env_lookup_at", "status": "conflict",
   "reason": "trunk hash differs", "trunk_hash": "B2C3D4..."}
]}
```

### Merging multiple changesets

```bash
glyph apply program.glyph cs_agent_a.json cs_agent_b.json
```

Before applying, cross-check for **cross-changeset conflicts**: if both changesets modify the same definition (even if neither conflicts with trunk), flag it. The merge tool presents both versions for resolution.

### MCP tools

| Tool | Purpose |
|------|---------|
| `changeset` | Compare two databases, return changeset JSON |
| `review` | Check changeset against trunk, report conflicts |
| `apply` | Apply changeset with conflict detection |

These enable the full Monticello-style workflow via MCP: an orchestrator agent creates branch copies, assigns them to worker agents, collects the resulting changesets, reviews them, and applies them to trunk.

### What this enables

**The Squeak Trunk workflow for Glyph:**

1. **Trunk** is the main `.glyph` database (e.g., `glyph.glyph`)
2. **Working copies** are `cp`'d databases — each agent or human gets their own
3. **Changesets** are the unit of submission — portable JSON documents
4. **Review** happens before merge — a human, an orchestrator, or even another LLM inspects the changeset
5. **Application** uses compare-and-swap — conflicts are detected, never silently overwritten
6. **History** is preserved — `def_history` records every change, changesets can be archived

This model works for:
- Multiple LLM agents working on different features in parallel
- Human-LLM collaboration (human reviews LLM's changeset before applying)
- Distributed development (changesets are files — email them, store them, version them)
- Rollback (a changeset can be inverted: adds become removes, removes become adds, modifies swap base/new body)

## Conflict Types and Resolution

### Write-write conflicts (solved by Layer 1)

Agent A and B both modify `foo`. The second write is rejected via `base_hash` mismatch. Agent B re-reads, examines A's changes, merges, and retries.

### Semantic conflicts (harder)

Agent A changes `parse_atom` to accept different arguments. Agent B is modifying `parse_expr`, which calls `parse_atom`. B's code compiles against the old `parse_atom` contract. This isn't a write conflict — B never touched `parse_atom`. It's a contract change that invalidates B's work.

**Detection:** After all agents complete, run `check_all`. Type errors at the boundary between two agents' work zones are semantic conflicts. The dep graph identifies exactly which pairs of modifications to examine:

```sql
-- Find definitions modified by different agents that share dependencies
SELECT h1.name AS def_a, h1.session_id AS agent_a,
       h2.name AS def_b, h2.session_id AS agent_b
FROM def_history h1
JOIN dep d ON d.from_id = (SELECT id FROM def WHERE name = h1.name)
JOIN def_history h2 ON d.to_id = (SELECT id FROM def WHERE name = h2.name)
WHERE h1.session_id != h2.session_id
  AND h1.changed_at > ?  -- within this work session
  AND h2.changed_at > ?;
```

**Prevention:** The orchestrator marks a definition's direct dependents as "co-assigned" — they must be handled by the same agent, or sequenced (A finishes first, B sees the updated contract).

## Implementation Path

### Phase 1 (~half session)

Layer 1 only. Compare-and-swap on `put_def`, hash in `get_def` responses, session-specific temp paths.

**Definitions to modify:**
- `mcp_tool_get_def` — include `hash` (hex) in JSON response
- `mcp_tool_get_defs` — include `hash` per definition in response
- `mcp_do_put` — add `BEGIN IMMEDIATE` transaction, compare-and-swap UPDATE, conflict detection
- `mcp_tool_put_def` — extract `base_hash` parameter, pass to `mcp_do_put`, enforce "required for updates"
- `mcp_tool_put_defs` — extract `base_hash` per definition
- `mcp_add_tools2` (or wherever `put_def` schema is) — add `base_hash` to JSON schema description
- `mcp_tool_build` — use PID-based temp paths
- `mcp_tool_run` — use PID-based temp paths

After this change, concurrent sessions are safe: blind overwrites are impossible because `put_def` on existing defs requires `base_hash`, and stale `base_hash` values are rejected by the database. The LLM can't opt out — it must read before writing.

### Phase 2 (~1 session)

Layer 2. Session/claim tables and MCP tools. Agents can see each other's work areas. Still requires manual task decomposition but conflict detection is automated.

**New definitions (~15-20):**
- Schema migration for session + claim tables
- `mcp_tool_register_session`, `mcp_tool_heartbeat`
- `mcp_tool_claim_defs`, `mcp_tool_release_defs`, `mcp_tool_list_claims`
- Tool schema registration and dispatch integration
- Stale session cleanup

### Phase 3 (ongoing)

Build an orchestrator. This is the intellectually interesting part — using the dep graph to automatically partition tasks and coordinate agents. It depends on the primitives from Phases 1 and 2.

## Why This Matters

The empirical analysis reveals something stronger than the theoretical argument: **the Glyph compiler's namespace structure already defines natural agent boundaries with near-zero behavioral coupling.** The cross-group interfaces are almost entirely immutable constructors. Three agents working on frontend/middle/backend would have 6 behavioral edges between them — 4 of which are calls to constants.

This isn't an accident. The prefix-based namespace system (`cg_` → codegen, `tc_` → typeck, `tok_` → tokenizer) reflects the actual coupling structure. The discipline of using MIR constructors as the shared vocabulary between compiler phases means the interfaces are inherently stable. Multi-agent work doesn't require *creating* boundaries — it requires *recognizing* the boundaries that already exist.

The database model makes Glyph programs **inherently collaborative artifacts** in a way file-based code cannot be. A `.glyph` file with session tracking, claims, and optimistic concurrency is closer to a shared document than a source file. Multiple agents — human-directed or autonomous — work on it simultaneously with definition-level conflict semantics.

Git's concurrency model is optimistic but operates at the line level with no semantic understanding. Glyph's operates at the definition level with full dependency-graph awareness. No merge conflicts, no rebasing, no "please pull before pushing."

For Glyph specifically, the numbers are compelling:
- **83% of functions** (1,158 of 1,399) are internal-only — free to modify with zero conflict risk
- **17% are interface functions**, the majority of which are immutable constructors
- **Tests partition cleanly** by namespace, with integration tests as a final pass
- The minimum viable protocol (Layer 1) is ~5-8 definition changes

This is the thing no file-based language can offer.