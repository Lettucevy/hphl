# HP-HL Language Reference

This document is the canonical reference for the HP-HL programming language. It covers all keywords, types, statements, expressions, and built-in facilities. Companion documents cover the [Standard Library](../stdlib/index.md), the [Migration guide](../migration.md), and the [Cookbook](../cookbook/index.md).

HP-HL is a statically-typed, compiled, garbage-collected systems language with first-class support for concurrency, memory policies, and contracts. Programs compile to native code via the `hphlc` compiler, with x64 as the primary target (LLVM and AArch64/WASM backends available).

> Version: v1.0.0 — Canonical language specification.

---

## Table of Contents

1. [Lexical structure](#lexical-structure)
2. [Types](#types)
3. [Declarations](#declarations)
4. [Statements](#statements)
5. [Expressions](#expressions)
6. [Memory policies](#memory-policies)
7. [Concurrency](#concurrency)
8. [Contracts and error handling](#contracts-and-error-handling)
9. [Modules and packages](#modules-and-packages)
10. [Generics](#generics)
11. [Attributes](#attributes)

---

## Lexical structure

### Comments

```hp-hl
// line comment
/* block comment (no nesting) */
```

### Identifiers

Start with a letter or underscore, followed by letters, digits, or underscores. Identifiers are case-sensitive.

### Keywords

The following are reserved words and cannot be used as identifiers:

```
module  import  using  package
class   struct  interface   enum   actor
public  private protected   static  inline
heap    shared   arena   pool   threadlocal   atomic
const   readonly   volatile   lock
async   await   spawn   parallel   foreach   batch
new     this     base     super
if      else    while    do     for    break    continue    return
match   case    default
try     catch   finally   throw   panic   assert
derive  where   specialize  reflect
true    false   null   void
int     float   bool   char   string   byte
u8 u16 u32 u64   i8 i16 i32 i64
list    map     set     tuple   option
task    channel mutex   semaphore   event   barrier
```

> **Note:** `uint` is not a reserved keyword in HP-HL. Use explicit unsigned width types `u8`, `u16`, `u32`, `u64`.

> **Note:** `pool` and `base` are contextual: they work as ordinary
> identifiers (variables, params, fields, functions) everywhere except their
> keyword positions (`pool` as storage policy in `pool T x`, `base` in
> `base.M()` calls). Types named `pool`/`base` are not supported.

See each keyword's section for details.

### Literals

```hp-hl
42            // int (32-bit default)
42u           // u32 unsigned literal
3.14          // float (64-bit default)
3.14f         // float (32-bit)
true          // bool
false         // bool
'a'           // char
"hello\n"     // string
0xFF          // hex
0b1010        // binary
0o755         // octal
1_000_000     // digit separator
```

---

## Types

### Primitive types

| Type     | Default Size | Description                          |
| -------- | ------------ | ------------------------------------ |
| `bool`   | 1 bit (1B)   | `true` or `false` (`bool<1>`, `bool<8>`) |
| `char`   | 1 byte       | UTF-8 code unit (`char<8>`, `char<16>`, `char<32>`) |
| `byte`   | 1 byte       | unsigned 8-bit                       |
| `i8`/`u8`   | 1 byte    | signed/unsigned 8-bit (`int<8>`)     |
| `i16`/`u16` | 2 bytes   | signed/unsigned 16-bit (`int<16>`)   |
| `i32`/`u32` | 4 bytes   | signed/unsigned 32-bit (`int<32>`)   |
| `i64`/`u64` | 8 bytes   | signed/unsigned 64-bit (`int<64>`)   |
| `int`    | 4 bytes      | default 32-bit signed int (`int<32>`)|
| `f32`    | 4 bytes      | IEEE-754 single precision (`float<32>`) |
| `f64`    | 8 bytes      | IEEE-754 double precision (`float<64>`) |
| `float`  | 8 bytes      | default double precision (`float<64>`)|
| `string` | pointer      | immutable UTF-8 string               |
| `void`   | 0            | no value                             |

### Bit-Parametrized Primitive Types

HP-HL allows specifying explicit bit widths on primitive types using angle brackets `<N>` (where `N` is the size in **bits**):

- **Integers (`int<N>`)**:
  - `int<1>` (1 bit flag)
  - `int<2>`, `int<4>`, `int<8>` (`i8`), `int<16>` (`i16`), `int<32>` (`i32` / default `int`)
  - `int<64>` (`i64`), `int<128>`, `int<256>`, `int<512>`, `int<1024>`
  - Arbitrary bit widths up to 8192 are supported by the compiler frontend and LLVM IR backend.
- **Floating-point (`float<N>`)**:
  - `float<16>`: 16-bit half precision (`half`)
  - `float<32>`: 32-bit single precision (`f32` / `float`)
  - `float<64>`: 64-bit double precision (`f64` / default `float`)
  - `float<128>`: 128-bit quad precision (`fp128`)
  - `float<256>`, `float<512>`, `float<1024>`
- **Characters (`char<N>`)**:
  - `char<8>`: 8-bit character / UTF-8 code unit (default `char`)
  - `char<16>`: 16-bit wide character / UTF-16 code unit
  - `char<32>`: 32-bit Unicode code point
- **Booleans (`bool<N>`)**:
  - `bool<1>`: 1-bit boolean
  - `bool<8>`: 8-bit byte-sized boolean (default `bool`)
- **Strings (`string<N>`)**:
  - `string<N>`: string with fixed-length / capacity specification.

### Composite types

| Type        | Example                  | Description                  |
| ----------- | ------------------------ | ---------------------------- |
| `list<T>`   | `list<int>`              | growable array                |
| `map<K,V>`  | `map<string, int>`       | hash map                      |
| `set<T>`    | `set<int>`               | hash set                      |
| `tuple<T1,T2,...>` | `(int, string)`   | heterogeneous tuple          |
| `option<T>` | `option<int>`            | nullable, `Some(T)` or `None`|
| `task<T>`   | `task<int>`              | async result                  |
| `channel<T>`| `channel<int>`           | typed channel                 |
| `T[N]`      | `int[10]`                | fixed-size array              |
| `T[N][M]`   | `int[3][4]`              | multi-dim array               |
| `lock T`    | `lock int`               | mutex-protected value         |
| `arena T`   | `arena int`              | arena-allocated value         |
| `shared T`  | `shared int`             | thread-safe ref-counted       |
| `pool T`    | `pool int`               | object pool                   |

### Type qualifiers

`const`, `readonly`, `volatile`, `atomic` — see [Attributes](#attributes).

### Type inference

Type inference is **local** (within expressions) but declarations require explicit types. There is no `var x = 1;` style global inference. You may use `var` to declare with type inference in function bodies:

```hp-hl
void Main() {
    var x = 1;      // x: int
    var s = "hi";   // s: string
}
```

---

## Declarations

### Module declaration

Every HP-HL file starts with a module declaration:

```hp-hl
module myproject.utils;
```

The module name maps to the file path: `myproject/utils.hphl`.

### Imports

```hp-hl
import std.io;                    // import whole module
import std.io.{print, readln};    // import specific symbols
import myproject.utils as u;      // alias
public use std.collections;       // re-export
```

### Variable declarations

```hp-hl
int x = 42;                       // mutable
const int y = 10;                 // compile-time constant
readonly int z = compute();       // runtime-initialized, immutable
heap int h = new int(5);          // heap-allocated
shared int s = new shared int(0); // thread-safe ref-counted
```

### Functions

```hp-hl
int Soma(int a, int b) {
    return a + b;
}

async task<int> Dobrar(int x) async {  // async returns task<T>
    return x * 2;
}

int Factorial(int n) {
    if (n <= 1) return 1;
    return n * Factorial(n - 1);
}
```

Parameters can be `ref` (by reference) or `out` (write-only):

```hp-hl
void Swap(ref int a, ref int b) {
    int t = a; a = b; b = t;
}

bool TryParse(string s, out int result) {
    // ...
    result = 0;
    return true;
}
```

### Classes and structs

```hp-hl
class Animal {
    public string Nome;
    private int Idade;

    public Animal(string nome, int idade) {
        Nome = nome; Idade = idade;
    }

    public virtual string Fala() {
        return "...";
    }
}

class Cachorro : Animal {
    public Cachorro(string nome) : base(nome, 0) {}

    public override string Fala() {
        return "Au au";
    }
}

struct Ponto {
    public int X;
    public int Y;
}
```

#### Access modifiers

`public`, `private`, `protected`. Default for class members is `private`; default for struct members is `public`.

#### Inheritance

A class can extend exactly one base class. Use `:` followed by the base class and (optionally) implemented interfaces:

```hp-hl
class Gato : Animal, IComparable, ISerializable {
    // ...
}
```

### Interfaces

```hp-hl
interface IComparable {
    int CompareTo(other: IComparable);
}

interface IRepository<T> {
    T Get(int id);
    void Save(T entity);
}
```

Generic interfaces are partially supported (warning for full monomorphization in v0.93).

### Enums

```hp-hl
enum Cor : int {
    Vermelho = 0,
    Verde = 1,
    Azul = 2
}

enum Result<T, E> {
    Ok(T value),
    Err(E error)
}
```

### Actors

Actors are stateful, message-passing concurrency units. They have a single thread of execution and process messages serially.

```hp-hl
actor Counter {
    private int n = 0;
    public int Inc() { n++; return n; }
}
```

---

## Statements

### Control flow

```hp-hl
if (x > 0) { /* ... */ } else { /* ... */ }

while (cond) { /* ... */ }

do { /* ... */ } while (cond);

for (int i = 0; i < 10; i++) { /* ... */ }

foreach (var x in list) { /* ... */ }

break;     // exit loop
continue;  // next iteration
return;    // exit function
```

### Pattern matching

```hp-hl
match (x) {
    0 => "zero",
    1 => "one",
    n if n > 0 => "positive",
    _ => "other"
}
```

```hp-hl
match (opt) {
    Some(v) => print(v),
    None => print("empty")
}
```

Patterns also work on `string` (content comparison) and `char`
(including ranges). A string/char subject requires a trailing `_`:

```hp-hl
match (c) {
    "HP" => 10,
    "" => 0,
    _ => -1,
}

match (ch) {
    'A' => 1,
    'a'..'z' => 2,
    _ => -1,
}
```

### Exception handling

```hp-hl
try {
    risky();
} catch (Exception e) {
    print(e.Message);
} finally {
    cleanup();
}
```

`throw` raises an exception. `panic(msg)` is for unrecoverable errors.

### Locking

```hp-hl
lock (resource) {
    // critical section
}
```

Nested locks produce a **warning** (potential deadlock) in v0.93.

### Parallel

```hp-hl
parallel foreach (var x in items) {
    process(x);
}

parallel {
    task1();
    task2();
    task3();
}
```

---

## Expressions

### Operators

| Category | Operators                                  |
| -------- | ------------------------------------------ |
| Arithmetic | `+  -  *  /  %` (int `/` truncates toward zero, C-style: `-7/2` is `-3`, `-7%2` is `-1`) |
| Comparison | `==  !=  <  >  <=  >=`                   |
| Logical    | `&&  ||  !`                              |
| Bitwise    | `&  \|  ^  ~  <<  >>`                    |
| Assignment | `=  +=  -=  *=  /=  %=  &=  \|=  ^=  <<=  >>=` |
| Misc       | `?.  ?[]  ?.  ::` (cast)                 |

### Ternary

```hp-hl
int x = cond ? a : b;
```

### String interpolation

```hp-hl
string s = $"Hello, {name}! You are {age} years old.";
```

### Function calls

```hp-hl
int r = Compute(1, 2);
int r2 = Compute(1, 2) async;  // returns task<int>
int v = await r2;              // blocks until done
```

### Lambdas (v0.95)

First-class anonymous functions with by-value captures:

```hp-hl
var inc = (x) => x + 1;              // inferred func<int, int>
var add = (a, b) => a + b;           // multi-param
var dbl = (x) => { int y = x * 2; return y; };  // block body
var id = (int x) => x;               // annotated param
func<int, int> sq = (x) => x * x;    // contextual type
println(inc(41));                    // 42 — indirect call
println(((x) => x + 1)(41));         // IIFE
```

Parameter types are inferred from use (literals, operators, declared
locals); annotate when ambiguous (`(s) => s` needs `(string s)`).
Arithmetic-only params default to `int`. The return type comes from
`return` statements (`void` if none).

```hp-hl
int off = 100;
var f = (x) => x + off;   // captures off BY VALUE (copy at creation)
off = 200;
f(1);  // 101 — later writes don't affect the copy
```

Values have type `func<R, P...>` (`func<int, int>`, `func<void>`),
passable as arguments and returnable from functions. Recursion needs an
explicit type so the name is visible in its own body:

```hp-hl
func<int, int> fact = (n) => n <= 1 ? 1 : n * fact(n - 1);
```

Limits (v1): captures must fit 8-byte slots (no local arrays — use heap);
closures must not outlive reference captures under GC pressure (same
rule as `spawn` captures); no currying/partial application.

---

## Memory policies

HP-HL has explicit memory policies on type modifiers:

| Policy    | Lifetime                    | Thread-safe | GC? |
| --------- | --------------------------- | ----------- | --- |
| `stack`   | lexical scope               | yes         | no  |
| `heap`    | GC-managed                  | yes         | yes |
| `shared`  | ref-counted, shared         | yes         | no  |
| `lock`    | GC + mutex                  | yes         | yes |
| `arena`   | arena-allocated (frame)     | no          | no  |
| `pool`    | object pool                 | yes         | depends |
| `threadlocal` | per-thread              | n/a         | no  |

Example:

```hp-hl
heap int h = new int(42);
shared int s = new shared int(0);
arena int a = 100;  // freed when arena resets
```

### Garbage collection

HP-HL uses a **conservative, moving, generational GC** (BDWGC-inspired). The GC runs cooperatively at allocation thresholds.

Use `atomic` for concurrent access without locks:

```hp-hl
atomic int contador = 0;
```

---

## Concurrency

### Spawn (task)

```hp-hl
spawn {
    print("runs in background");
};
```

### Async/await

```hp-hl
async int Compute(int x) {
    await Task.Delay(100);
    return x * 2;
}

void Main() {
    task<int> t = Compute(21);
    int r = await t;  // 42
}
```

### Channels

```hp-hl
channel<int> ch = new channel<int>(10);  // buffered

spawn { ch.Send(1); };
int x = ch.Receive();
```

### Mutex / Lock

```hp-hl
lock T counter = 0;
spawn { lock (counter) { counter++; } };
```

### Events and barriers

```hp-hl
event e = new event();
e.Set();
e.Wait();
```

---

## Contracts and error handling

### Assert

```hp-hl
assert(x > 0, "x must be positive");
```

### Panic

```hp-hl
panic("unreachable");
```

### Pre/post conditions

```hp-hl
int Divide(int a, int b)
    require b != 0
    ensure result >= 0
{
    return a / b;
}
```

- `require cond` (one or more): checked on entry; `ensure cond` (one or
  more): checked on each `return` (including multiple exit points).
- `result` is only valid inside `ensure` and represents the returned value
  (in a `void` function, using `result` is a semantic error; outside `ensure`,
  `result` is a regular identifier).
- Contract violations abort via `assert` on the line of the clause.
- Valid for functions, methods (`this` is accessible) and constructors
  (`ensure` without `result`, e.g., invariants on `this`).

---

## Modules and packages

### Module file

```hp-hl
// myproject/utils.hphl
module myproject.utils;

public int Helper(int x) { return x + 1; }
```

### Package manifest (`hphl.pkg.toml`)

```toml
[package]
name = "myproject"
version = "0.1.0"
entry = "src/main.hphl"
runtime = "0.93.0"

[dependencies]
std = "0.93.0"
```

---

## Generics

```hp-hl
class Caixa<T> {
    public T Valor;
    public Caixa(T v) { Valor = v; }
}

void Main() {
    Caixa<int> c = new Caixa<int>(42);
    print(c.Valor);
}
```

### Constraints (`where`)

Constraints can be applied to type parameters on functions, methods, and classes:

```hp-hl
// Supported constraints:
// - class: reference types (class, string, list, map, etc.)
// - struct: value types (primitives, custom structs)
// - new(): parameterless default constructor
// - supports <op>: operator support (+, -, *, /, %, ==, !=, <, >, <=, >=)
// - IComparable, Equatable, Hashable, Cloneable, Sendable
// - BaseClassName, InterfaceName

class Box<T> where T : class {
    public T Val;
    public Box(T v) { Val = v; }
}

T CreateNew<T>() where T : new(), class {
    return new T();
}

T TakeStruct<T>(T v) where T : struct {
    return v;
}

T Max<T>(T a, T b) where T : IComparable {
    return a > b ? a : b;
}
```

### Variance (`in` / `out`)

Interfaces support declaration-site variance for type parameters:
- `out T` (covariance): `T` can only be returned (output position). An `IProducer<Derived>` is assignable to `IProducer<Base>`.
- `in T` (contravariance): `T` can only be accepted as parameter (input position). An `IConsumer<Base>` is assignable to `IConsumer<Derived>`.
- `func<R, P...>` function types also support natural variance (covariant return, contravariant parameters).

```hp-hl
interface IProducer<out T> {
    T Produce();
}

interface IConsumer<in T> {
    void Consume(T item);
}

class DogProducer : IProducer<Dog> {
    public Dog Produce() { return new Dog(); }
}

class AnimalConsumer : IConsumer<Animal> {
    public void Consume(Animal a) { println(a.Name); }
}

void Main() {
    // Covariance: Dog -> Animal
    IProducer<Animal> p = new DogProducer();
    Animal a = p.Produce();

    // Contravariance: Animal consumer accepts Dog
    IConsumer<Dog> c = new AnimalConsumer();
    c.Consume(new Dog());
}
```

### Value parameters

```hp-hl
class Buffer<T, const int N> {
    T[N] data;
}
```

---

## Attributes

HP-HL supports the following attributes:

| Attribute              | Purpose                                    |
| ---------------------- | ------------------------------------------ |
| `[[deprecated("...")]]` | Mark as deprecated, warn at use site      |
| `[[inline]]`           | Hint to inline                             |
| `[[noreturn]]`         | Function never returns                     |
| `[[packed]]`           | Struct without padding                     |
| `[[no_gc]]`            | Type does not need GC tracing              |
| `[[suppress_deprecation]]` | Suppress deprecation warnings in scope  |

### Deprecated example

```hp-hl
[[deprecated("use NewName instead")]]
int OldName() { return 0; }

void Main() {
    int x = OldName();  // warning: 'OldName' is deprecated
    [[suppress_deprecation]]
    {
        int y = OldName();  // no warning
    }
}
```

---

## Reserved Names

- Names starting with `__` are reserved for the compiler and runtime.
- Names starting with `hphl_` in the runtime API are reserved for the runtime.

---

## See Also

- [Standard Library](../stdlib/index.md) — built-in functions and types
- [Cookbook](../cookbook/index.md) — common patterns
- [Migration guide](../migration.md) — porting from C++/Rust
- [Installation & Tooling](../installation.md) — editor setup and compiler
- [Compatibility policy](../COMPAT.md) — versioning and stability
