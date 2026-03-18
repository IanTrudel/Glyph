# Glyph Portability Assessment

> **Scope**: What it would take to make Glyph build and run on platforms beyond Linux x86-64.
>
> **Date**: 2026-03-15
> **Status**: Research/assessment only — no code changes.

---

## Compilers in Scope

Glyph has two compilers with different portability profiles:

| Compiler | Backend | Primary use |
|---|---|---|
| **Self-hosted** (`glyph.glyph` → `./glyph`) | C codegen — emits C99, invokes system `cc` | Primary compiler; all user programs |
| **Rust bootstrap** (`glyph0`, `cargo build`) | Cranelift native codegen | Bootstrap only; builds the self-hosted compiler |

These are assessed separately. The Rust bootstrap is inherently more portable (Cranelift supports multiple ISAs and Rust abstracts OS differences); the interesting portability work is in the **self-hosted compiler**.

---

## Self-Hosted Compiler (glyph.glyph)

The self-hosted compiler generates C99 source code and pipes it to `cc`. This architecture is **inherently retargetable**: by setting `CC=<cross-compiler>`, the compiler can produce binaries for any architecture the C compiler targets, with zero changes to the Glyph compiler itself. The portability issues below are mostly about Windows compatibility and robustness rather than fundamental architecture limits.

### 1. Hardcoded `/tmp` Paths — HIGH (blocks Windows)

Three functions write to hardcoded Unix paths:

| Function | Path |
|---|---|
| `build_program` | `/tmp/glyph_out.c` |
| `build_test_program` | `/tmp/glyph_test.c` |
| `cmd_run` | `/tmp/glyph_run_tmp` |
| `cmd_import` | `/tmp/glyph_import_files.txt` |
| `cmd_test` | `/tmp/glyph_test_bin` |

Windows has no `/tmp`. The equivalent is `%TEMP%` (e.g. `C:\Users\user\AppData\Local\Temp`).

**Fix**: Add a `glyph_temp_dir()` runtime function that returns `$TMPDIR` on Unix or `%TEMP%` on Windows. Alternatively, read `TMPDIR`/`TEMP` env vars directly via `glyph_getenv()` (also a useful general-purpose addition).

### 2. `-no-pie` Linker Flag — HIGH (blocks macOS, MSVC)

`build_program` and `build_test_program` append `-no-pie` unconditionally:
```
glyph_system(s5("cc ", cc_flags, " ", c_path, " -o ", output_path, " -no-pie", lib_flags, " -lm"))
```

`-no-pie` is a Linux-specific GCC/Clang flag. Apple clang rejects it with:
```
error: unsupported option '-no-pie'
```
MSVC doesn't recognize it at all.

**Fix**: Remove it. Modern Linux systems default to PIE but the emitted C code doesn't require position-dependent addressing. The flag was a conservative workaround; removing it is safe on all platforms including Linux.

### 3. `glyph_system()` Exit Code Extraction — MEDIUM (breaks Windows)

In `cg_runtime_io`, the generated C runtime decodes exit codes with:
```c
int rc = system(c);
return (long long)((rc >> 8) & 0xFF);
```

This is Unix's `WEXITSTATUS` convention: the child exit code lives in bits 15–8 of the `waitpid` status. On Windows, `system()` returns the exit code directly without bit-shifting, so `(rc >> 8)` would always return 0 for success.

**Fix**:
```c
#ifdef _WIN32
  return (long long)rc;
#else
  return (long long)((rc >> 8) & 0xFF);
#endif
```

### 4. POSIX Signal Handlers — MEDIUM (breaks Windows)

The generated C preamble (`cg_main_wrapper`) installs Unix signal handlers:
```c
signal(SIGSEGV, _glyph_sigsegv);
signal(SIGFPE,  _glyph_sigfpe);
```

`SIGSEGV` (signal 11) is POSIX-only. On Windows, access violations are structured exceptions (SEH), not signals. `signal(SIGSEGV, ...)` on Windows is a no-op or crashes the process.

**Fix**: Guard with `#ifndef _WIN32`, or implement Windows SEH equivalent:
```c
#ifndef _WIN32
  signal(SIGSEGV, _glyph_sigsegv);
  signal(SIGFPE,  _glyph_sigfpe);
#endif
```

### 5. No `CC` Env Var in Self-Hosted Compiler — MEDIUM (all platforms)

The Rust bootstrap already respects `CC`:
```rust
let cc = std::env::var("CC").unwrap_or_else(|_| "cc".to_string());
```

But the self-hosted compiler hard-codes `"cc"` in the `glyph_system()` invocation. Custom toolchains (`musl-gcc`, `zig cc`, `arm-linux-gnueabihf-gcc`) require overriding the compiler name.

**Fix**: Add `glyph_getenv(name)` runtime function. Read `CC` env var in `build_program`/`build_test_program` and fall back to `"cc"` if unset:
```
cc_cmd = match glyph_getenv("CC") / "" -> "cc" / s -> s
```

### 6. 64-Bit ABI Assumption in Generated C Runtime — MEDIUM (breaks 32-bit)

The C runtime uses hardcoded byte offsets assuming 8-byte pointers:
```c
// String struct: {ptr, len} — offset 8 assumes 64-bit pointer
*(long long*)((char*)s + 8)

// Array header: {data, len, cap} — offsets 8, 16
long long* h = (long long*)hdr; h[1] /* len */ h[2] /* cap */
```

On 32-bit systems (ARM32, x86), `sizeof(void*) == 4`, making these offsets wrong. The `GVal = intptr_t` type is correctly sized, but the hardcoded `+8` breaks struct access.

**Fix for 32-bit**: Replace `+8` offsets with `sizeof(GVal)` arithmetic:
```c
#define GVAL_SIZE ((int)sizeof(GVal))
// String len:
*(GVal*)((char*)s + GVAL_SIZE)
```

This is a non-trivial change touching many runtime functions. **Low priority** — 32-bit Glyph is not a near-term target.

### 7. `-lm` Always Linked — LOW (cosmetic on macOS/Windows)

`-lm` links the POSIX math library. On macOS, it's a no-op (math is in `libSystem`). On Windows with MinGW, it exists but is unnecessary. With MSVC, the flag is unrecognized.

**Fix**: Only append `-lm` when the program uses math functions (already tracked via `collect_libs`), or skip it on macOS. Not a blocker.

### 8. POSIX Shell Commands in `cmd_import` and `cmd_export` — LOW

Two commands use Unix shell tools directly:
```glyph
-- cmd_import uses: find ... | sort
-- cmd_export uses: mkdir -p
```

These are not portable to Windows without Unix emulation (Cygwin/WSL).

**Fix**: Implement file listing and directory creation as native runtime functions (`glyph_list_files`, `glyph_mkdir_p`) rather than shell invocations.

---

## Retargetability: The C Codegen Advantage

The self-hosted compiler's C codegen backend is **inherently retargetable** for free:

1. Set `CC=arm-linux-gnueabihf-gcc` → compiles Glyph programs for 32-bit ARM Linux
2. Set `CC=aarch64-apple-darwin-clang` → compiles for Apple Silicon
3. Set `CC=zig cc --target=wasm32-wasi` → compiles for WASI WebAssembly
4. Set `CC=x86_64-w64-mingw32-gcc` → cross-compiles for Windows (once `/tmp` is fixed)

Zero Glyph compiler changes required. **This is the primary retargeting path.**

### LLVM IR / MLIR as Alternative Backend

An alternative portability strategy is replacing C codegen with an **LLVM IR or MLIR backend** in the self-hosted compiler. This would:

- Eliminate all C compiler dependency (no `cc` invocation at all)
- Enable true cross-compilation via LLVM targets
- Allow optimization passes via LLVM's optimizer
- Give direct control over calling conventions per target

The current C codegen approach delegates these concerns to `cc`, which is both a strength (simplicity, portability through the C compiler's own retargetability) and a limitation (can't control optimization passes, limited visibility into ABI details).

**Cost of LLVM IR backend**: LLVM IR syntax is well-specified and not that dissimilar to MIR in structure. The self-hosted compiler already generates ~300 lines of C per compilation unit; an equivalent LLVM IR emitter would be comparable in scope but more verbose. The primary new complexity is explicit handling of LLVM type declarations and calling convention attributes.

**Cost of MLIR backend**: Higher complexity — MLIR dialect design is non-trivial. Not recommended until the language stabilizes further.

**Recommendation**: The C codegen path via `CC=<cross-compiler>` is sufficient for near-term portability goals. An LLVM IR backend is the right long-term play if Glyph needs to run in environments without a C compiler (embedded, WASM runtimes, etc.).

---

## Rust Bootstrap Compiler (glyph0)

> The bootstrap compiler's portability matters less than the self-hosted compiler's, since glyph0 is only used to rebuild `./glyph` and can be rebuilt from source on any supported platform. Rust itself is highly portable.

### B.1 `-no-pie` in Linker — HIGH (blocks macOS)

`crates/glyph-codegen/src/linker.rs:52`:
```rust
cc_args.push("-no-pie".to_string());
```

Same issue as the self-hosted compiler. Apple clang rejects this.

**Fix**: Conditionalize:
```rust
if cfg!(target_os = "linux") {
    cc_args.push("-no-pie".to_string());
}
```

### B.2 `CallConv::SystemV` Hardcoded — HIGH (breaks Windows)

`crates/glyph-codegen/src/abi.rs:31`:
```rust
let call_conv = CallConv::SystemV;
```

`SystemV` is the calling convention for Unix/Linux/macOS x86-64. Windows x86-64 uses Microsoft's `__fastcall`-derived convention (`CallConv::WindowsFastcall`). Any extern calls (to C runtime functions) would pass arguments in wrong registers on Windows.

**Fix**:
```rust
let call_conv = if cfg!(target_os = "windows") {
    CallConv::WindowsFastcall
} else {
    CallConv::SystemV
};
```

### B.3 `cranelift_native::builder()` — Host-Only, No Cross-Compilation

`crates/glyph-codegen/src/cranelift.rs:37`:
```rust
let isa = cranelift_native::builder()
    .expect("host ISA not available")
    .finish(flags)
    .unwrap();
```

Cranelift detects the host ISA at compile time. `glyph0` can only compile for the architecture it was compiled on. There is no `--target` flag.

This is acceptable for a bootstrap tool — you just need to compile `glyph0` on each target platform. The self-hosted compiler then inherits retargetability via `CC=<cross-compiler>`.

**Fix for explicit cross-compilation**: Add `--target <triple>` to CLI, replace `cranelift_native::builder()` with `cranelift_codegen::isa::lookup_by_name(triple)`. Low priority.

### B.4 64-Bit Layout Assumptions — MEDIUM (breaks 32-bit)

`crates/glyph-codegen/src/layout.rs`:
```rust
MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _) => 8,
MirType::Str => 16,    // {ptr(8), len(8)}
MirType::Array(_) => 24,  // {ptr(8), len(8), cap(8)}
```

Hardcoded 8-byte sizes throughout. On 32-bit targets, pointers are 4 bytes.

Same fundamental issue as the self-hosted runtime. **Low priority** for the same reason.

### B.5 Temp File Handling — Already Portable ✓

`linker.rs:23`: `tempfile::tempdir()` — uses the `tempfile` crate, which is cross-platform.

### B.6 `CC` Env Var — Already Respected ✓

`linker.rs:62`: `std::env::var("CC").unwrap_or_else(|_| "cc".to_string())` — already portable.

---

## Build System

| Issue | Severity | Notes |
|---|---|---|
| `install -Dm755` uses POSIX `install` | Low | Build rule for `ninja install`; not needed for development |
| `build.ninja` rules use Bash syntax | Low | `[ -f ... ]`, pipes — not Windows cmd-compatible |
| `sqlite3 glyph.glyph .dump` in dump rule | Low | Requires SQLite CLI on PATH |

The build system is Unix-only, but this matters primarily for the bootstrap workflow. The self-hosted compiler's output (`./glyph`) doesn't depend on ninja.

---

## Summary Table

| Issue | Severity | Affects | Location |
|---|---|---|---|
| Hardcoded `/tmp` paths | **High** | Windows | `build_program`, `build_test_program`, `cmd_run`, `cmd_import`, `cmd_test` |
| `-no-pie` linker flag | **High** | macOS, MSVC | `build_program`, `build_test_program`, `linker.rs:52` |
| `CallConv::SystemV` hardcoded | **High** | Windows (glyph0 extern calls) | `abi.rs:31` |
| Exit code extraction `(rc>>8)&0xFF` | **Medium** | Windows | `cg_runtime_io` |
| POSIX signal handlers | **Medium** | Windows | `cg_main_wrapper`, `cg_runtime_c` |
| No `CC` env var in self-hosted | **Medium** | Custom toolchains | `build_program`, `build_test_program` |
| 64-bit ABI hardcoded | **Medium** | 32-bit systems | `cg_runtime_c`, `layout.rs` |
| POSIX shell in `cmd_import`/`cmd_export` | **Low** | Windows | `cmd_import`, `cmd_export` |
| `-lm` always linked | **Low** | MSVC | `build_program`, `build_test_program` |
| Build system POSIX-only | **Low** | Windows | `build.ninja` |
| No cross-compilation in glyph0 | **Low** | Cross-compile scenarios | `cranelift.rs:37` |

---

## Portability Tiers

### Tier 1: macOS (2 changes, minimal effort)

1. Remove `-no-pie` from `build_program`, `build_test_program`, and `linker.rs`
2. No other blockers — SystemV ABI is used on macOS x86-64; `cranelift_native` auto-detects Apple Silicon AArch64

### Tier 2: Linux AArch64 (already works)

The self-hosted compiler targets whatever `cc` compiles for. `cranelift_native` auto-detects AArch64. No changes needed.

### Tier 3: Windows (6 changes, moderate effort)

1. Replace `/tmp` with `glyph_temp_dir()` runtime function reading `%TEMP%`
2. Fix exit code extraction: `(rc >> 8)` → `rc` on Windows
3. Guard POSIX signals with `#ifndef _WIN32`
4. Remove `-no-pie` (already needed for macOS above)
5. Fix `CallConv::WindowsFastcall` in `abi.rs` (glyph0 only)
6. Adapt build system for Windows shell (ninja rules, `cmd_import`/`cmd_export`)

Windows also requires a C compiler in PATH. MinGW-w64 or MSVC both work once the flags are fixed.

### Tier 4: Cross-Compilation via C Backend (already works — set `CC`)

Set `CC=<cross-compiler-triple>` when invoking `./glyph build`. The C codegen path targets whatever `cc` targets. The bootstrap (`glyph0`) remains host-only until a `--target` flag is added.

### Tier 5: 32-Bit Systems (substantial ABI redesign)

Redesign the string/array ABI to use `sizeof(GVal)`-sized offsets instead of hardcoded `+8`. Not recommended until a concrete 32-bit use case emerges.

### Tier 6: LLVM IR / MLIR Backend (long-term, eliminates `cc` dependency)

Add an LLVM IR emitter to the self-hosted compiler as an alternative codegen backend alongside C. This eliminates the dependency on an external C compiler, enables LLVM optimizations, and gives first-class cross-compilation. Reasonable scope — LLVM IR emission is structurally similar to the existing C emission, plus type declarations and calling convention attributes.
