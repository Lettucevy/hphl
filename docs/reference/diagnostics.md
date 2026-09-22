# Diagnostics, Error Codes & Localization

The HP-HL compiler (`hphlc`) implements a modern diagnostic engine providing precise error messages with file, line, and column coordinates, compatible with developer terminals, IDEs, and editors via the **Language Server Protocol (LSP)**.

---

## Languages and Localization (i18n / l10n)

The compiler supports compiler diagnostics in multiple languages. By default, **English (`en`)** is used, with built-in support for **Brazilian Portuguese (`ptbr`)** and **Spanish (`es`)**:

### 1. Via Command Line:
```bash
# English (default)
hphlc app.hphl --language en

# Brazilian Portuguese
hphlc app.hphl --language ptbr

# Spanish
hphlc app.hphl --language es
```

### 2. Via Environment Variable:
Set the `HPHL_LANG` environment variable:
```bash
export HPHL_LANG="en"
```

---

## Standard Diagnostic Format

All diagnostics follow the standard compiler diagnostic format compatible with VS Code, Cursor, and Unix/Windows toolchains:

```text
<filepath>:<line>:<column>: error: <error description>
```

Example:
```text
main.hphl:14:5: error: type mismatch: expected 'int', found 'string'
```

---

## Diagnostic Categories

### 1. Syntax Errors (Parser)
Reported when the lexical or grammatical structure violates the language specification.

| Message Key | Meaning | Remediation |
| :--- | :--- | :--- |
| `parser_expected` | Expected token was not encountered at current position. | Check for missing semicolons (`;`), parentheses, or delimiters. |
| `parser_unexpected_token` | Unexpected token in the grammatical stream. | Check typos or keywords used out of context. |
| `parser_unclosed_paren` | Opening parenthesis `(` was not closed with `)`. | Add the matching closing parenthesis. |
| `parser_unclosed_brace` | Opening brace `{` was not closed with `}`. | Close the matching code block. |
| `parser_unclosed_bracket` | Opening bracket `[` was not closed with `]`. | Verify index expressions or array literals. |

---

### 2. Semantic and Type System Errors
Emitted during type checking, scope resolution, and AST validation passes.

| Message Key | Meaning | Remediation |
| :--- | :--- | :--- |
| `sem_var_not_found` | Referenced identifier was not declared in current scope. | Declare variable before use or check spelling. |
| `sem_var_already_declared` | An identifier with this name already exists in current scope. | Rename identifier or remove duplicate declaration. |
| `sem_type_mismatch` | Incompatible types in assignment, return, or argument. | Cast explicitly or adjust expression types. |
| `sem_class_duplicate` | Duplicate class declaration with the same identifier. | Rename the class or check redundant module imports. |
| `sem_method_not_found` | Method invoked does not belong to the target type. | Verify method signature and access visibility (`public`). |
| `sem_field_not_found` | Field accessed does not exist in struct or class. | Inspect struct/class member definitions. |
| `sem_arg_count_mismatch` | Incorrect number of arguments passed in function call. | Pass the exact number of parameters expected by signature. |

---

### 3. Memory Policies & Lifecycle Validation
The compiler verifies that types adhere to their declared storage policies:

- **Classes on Stack:** Classes use GC-managed reference semantics and cannot declare a `stack` memory policy. For flat stack allocation, use `struct`.
- **ThreadLocal:** `threadlocal` variables are strictly supported in global/module scope.
- **Synchronization:** Primitives such as `mutex` and `barrier` must be declared locally to allow deterministic initialization and cleanup.

---

### 4. Concurrency and Multithreading
- **By-Value Semantics in `async`:** `ref`, `out`, and `in` parameter qualifiers are disallowed in async tasks because data is transferred between thread boundaries by value to prevent race conditions.
- **Sendable Types Across Channels:** Only immutable types, primitives, pure structs, or classes with `derive Sendable` can pass through a `channel<T>`.

---

### 5. Foreign Function Interface (FFI v2)
- **Missing Shared Library:** If a shared library specified in `extern "name"` is not found in the system PATH, the linker or dynamic loader emits an unresolved symbol error.
- **ABI Mismatch:** Functions declared with `extern` must match the platform x64 C ABI calling convention.
