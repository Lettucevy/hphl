# HP-HL Compatibility Policy

This document describes the stability guarantees and breaking-change policy for the HP-HL language, compiler (`hphlc`), and runtime (`hphl-runtime`).

HP-HL follows [Semantic Versioning 2.0.0](https://semver.org/) starting with the **v1.0.0** release. Versions before v1.0.0 (e.g. v0.93.x) are **pre-1.0** and follow a more permissive policy described below.

---

## Versioning Scheme

Each release uses the form `vMAJOR.MINOR.PATCH`:

- **MAJOR** — incompatible API/ABI/syntax changes.
- **MINOR** — new features, backwards-compatible.
- **PATCH** — bug fixes, backwards-compatible.

Each component has its own version:

| Component     | Current    | Where it lives                          |
| ------------- | ---------- | --------------------------------------- |
| `hphlc`       | v0.93.x    | `compiler/src/main.cpp` (`HPHL_VERSION`)|
| `hphl-runtime`| v0.93.x    | `runtime/VERSION`                       |
| ABI           | `100`      | `compiler/src/runtime/runtime.h`       |
| Language spec | v0.93      | `docs/specs/HpHl.pdf`                   |

The compiler and runtime share a version number until v1.0.0. After v1.0, they MAY diverge in PATCH (e.g. compiler v1.2.3 + runtime v1.2.5).

---

## Runtime ABI Stability

The **runtime ABI** is a separate dimension. It is identified by a single integer `HPHL_RUNTIME_ABI_VERSION` defined in `compiler/src/runtime/runtime.h`:

```c
#define HPHL_RUNTIME_ABI_VERSION 100   // v1.0
```

The compiler emits a call to `hphl_runtime_abi_version()` at program startup. If the returned value does not equal the value the program was compiled with, the runtime aborts the program with the message:

```
HP-HL runtime ABI mismatch (compiled with 100, expected by runtime)
```

### ABI Bump Policy

- The ABI is bumped **only on the following changes**:
  1. Renaming or changing the signature of any function in `runtime_api.h`.
  2. Changing the layout, size, or alignment of any runtime struct visible to the compiler-emitted code (e.g. `HphlTask`, `HphlList`, `HphlMap`).
  3. Changing the calling convention of any runtime function (which registers hold arguments, etc.).
  4. Removing a function from `runtime_api.h` that is still called by any compiler version.

- Pure implementation changes (inlining, algorithm swap, internal data structure) **do NOT** bump the ABI.

- When the ABI is bumped, both `HPHL_RUNTIME_ABI_VERSION` and the runtime function `hphl_runtime_abi_version()` MUST be updated in the same commit, and the `runtime/VERSION` file MUST be updated to match.

- The ABI version is communicated to the program as a single integer; `100` = v1.0, `200` = v2.0, etc.

---

## Pre-1.0 Policy (v0.x)

Versions before v1.0.0 are **pre-stable**. The following is permitted without warning:

- New keywords can be added.
- Existing keywords can change semantics with a deprecation warning in the same release.
- Library functions can be renamed or have signatures changed.
- The runtime ABI can be bumped at MINOR versions.

**Programs written for v0.x are NOT guaranteed to compile on a later v0.y** without source modifications. The compiler will emit a warning when it detects the use of features that have changed.

To upgrade a v0.x program, see `docs/migration.md`.

---

## v1.0.0 Guarantees

Starting with v1.0.0, the following is guaranteed:

### Compiler (`hphlc`)

- **Language syntax is frozen.** New keywords MAY be added but MUST NOT conflict with existing tokens.
- **Source files written for v1.x compile unchanged on any later v1.y release.**
- **The set of compiler warnings MAY grow**; warnings never become errors in a MINOR release.
- **New error codes MAY be added** to diagnostics.
- **The compiler CLI flags documented in `--help` are stable** within v1.x. New flags MAY be added.

### Runtime ABI

- The ABI is stable. Programs compiled for v1.0.0 run on any v1.x runtime.
- The runtime exposes the function `hphl_runtime_abi_version()` returning `100` for all v1.x.
- A v1.x program linked against a v1.0 runtime MUST run unchanged on a v1.5 runtime (if available).
- Internal data structures and algorithms are not part of the ABI and can change.

### Standard Library

- Functions in the standard library follow the same stability rules as the language.
- Deprecations are signaled with `[[deprecated("use X instead")]]` and produce a **warning**, not an error.
- Deprecation warnings remain for at least one MINOR release before the function is removed in the next MAJOR.

### Standard Library Removal (v2.0)

A function MAY be removed in v2.0 only if:
1. It was marked `[[deprecated]]` in a previous v1.x release, AND
2. The deprecation warning has been present for at least one full MINOR release.

---

## Deprecation Policy

The `[[deprecated("reason")]]` attribute is used to mark functions, methods, and language features that will be removed in a future version.

Example (runtime function):

```c
__attribute__((deprecated("use hphl_list_new_ex instead")))
void* hphl_list_new(void);
```

When a deprecated symbol is used, the compiler emits a warning:

```
warning: 'hphl_list_new' is deprecated: use hphl_list_new_ex instead
```

The warning is generated at the **use site**, not the declaration site. The user can suppress the warning with `[[suppress_deprecation]]` in source code (see the Language Reference for details).

---

## Supported Upgrade Paths

| From    | To      | Path            |
| ------- | ------- | --------------- |
| v0.90.x | v0.91.x | Automatic (no changes) |
| v0.91.x | v0.92.x | Automatic (warning for renamed functions) |
| v0.92.x | v0.93.x | Automatic (warning for new keyword usage) |
| v0.93.x | v1.0.0  | Manual (see `migration.md` for required changes) |
| v1.0.0  | v1.x.x  | Automatic (source-compatible) |
| v1.x.x  | v2.0.0  | Manual (breaking changes allowed; see `migration.md`) |

---

## Questions / Proposals

For proposals that would change the ABI, the language syntax, or the standard library API, open an issue on GitHub with the `compatibility` label. All ABI-bumping changes must include a migration entry in `docs/migration.md` in the same release that introduces the bump.
