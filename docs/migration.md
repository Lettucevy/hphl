# Migration guide: C++ and Rust → HP-HL

This document maps the most common C++ and Rust constructs to their HP-HL equivalents. It assumes you know one of the source languages and are learning HP-HL.

> **Source languages covered:** C++17, C++20, Rust 2021.
>
> **HP-HL version:** v0.93.0

The mapping is opinionated: HP-HL has its own idioms and you should not try to mechanically translate C++/Rust. Use this guide as a starting point, then read the [Language Reference](reference/language.md) and the [Cookbook](cookbook/index.md) to learn the idiomatic HP-HL way.

---

## 1. Hello, world

**C++**

```cpp
#include <iostream>
int main() {
    std::cout << "Hello, world!" << std::endl;
    return 0;
}
```

**Rust**

```rust
fn main() {
    println!("Hello, world!");
}
```

**HP-HL**

```hp-hl
module hello;
void Main() {
    print("Hello, world!\n");
}
```

---

## 2. Variables and types

| Concept              | C++                          | Rust                  | HP-HL                          |
|----------------------|------------------------------|-----------------------|--------------------------------|
| Mutable integer      | `int x = 42;`                | `let mut x = 42;`     | `int x = 42;`                  |
| Immutable            | `const int x = 42;`          | `let x = 42;`         | `const int x = 42;`            |
| Type inference       | `auto x = 42;`               | `let x = 42;`         | `var x = 42;` (local only)     |
| Floating point       | `double x = 3.14;`           | `let x = 3.14;`       | `float x = 3.14;`              |
| String               | `std::string s = "hi";`      | `let s = "hi";`       | `string s = "hi";`             |
| Array (fixed)        | `int a[10];`                 | `let a: [i32; 10];`    | `int[10] a;`                   |
| Vector               | `std::vector<int> v;`        | `let v: Vec<i32>;`    | `list<int> v = new list<int>();`|

> **Note:** HP-HL is **explicitly typed** in declarations; only `var` in function bodies allows inference.

---

## 3. Ownership and memory

| Concept                  | C++                                          | Rust                      | HP-HL                              |
|--------------------------|----------------------------------------------|---------------------------|------------------------------------|
| Stack value              | `int x = 1;`                                 | `let x = 1;`              | `int x = 1;`                       |
| Heap (GC)                | `auto* p = new int(1); delete p;`            | `Box::new(1)`             | `heap int p = new int(1);`         |
| Heap (no GC, refcounted) | `std::shared_ptr<int> p;`                    | `Rc<i32>` (single-thread) | `shared int p = new shared int(1);`|
| Heap (no GC, atomic rc)  | `std::shared_ptr<int> p;`                    | `Arc<i32>`                | `shared int p = new shared int(1);`|
| Unique ownership         | `std::unique_ptr<int> p;`                    | `Box<i32>`                | `heap int p = new int(1);`         |
| Arena / frame            | `auto* p = arena_alloc(...);`                | `bumpalo::Bump`           | `arena int p = 1;`                 |
| Thread-local             | `thread_local int x;`                        | `thread_local!`           | `threadlocal int x = 0;`           |
| Mutex                    | `std::mutex<int> m; m.lock();`               | `Mutex<i32>`              | `lock int m = 0;` or `lock (m) {}` |

**Key insight:** in C++ you choose between `unique_ptr` and `shared_ptr`. In Rust you choose between `Box`, `Rc`, `Arc`, and `Mutex`. In HP-HL, the **storage policy is a type modifier** that the compiler enforces. The default is `stack`; everything else is explicit.

---

## 4. Functions

**C++**

```cpp
int Soma(int a, int b) { return a + b; }
```

**Rust**

```rust
fn soma(a: i32, b: i32) -> i32 { a + b }
```

**HP-HL**

```hp-hl
int Soma(int a, int b) { return a + b; }
```

### By-reference parameters

**C++**

```cpp
void Swap(int& a, int& b) { int t = a; a = b; b = t; }
```

**Rust**

```rust
fn swap(a: &mut i32, b: &mut i32) { let t = *a; *a = *b; *b = t; }
```

**HP-HL**

```hp-hl
void Swap(ref int a, ref int b) { int t = a; a = b; b = t; }
```

### Default arguments

**C++** has default arguments in declarations. **Rust** has no default arguments (use overloads). **HP-HL** supports default arguments:

```hp-hl
int Power(int x, int exp = 2) {
    int r = 1;
    for (int i = 0; i < exp; i++) r = r * x;
    return r;
}
```

### Async functions

**C++** has no language-level async (use libraries). **Rust** has `async fn`. **HP-HL** has `async`:

```hp-hl
async int SlowDouble(int x) { return x * 2; }
void Main() {
    task<int> t = SlowDouble(21);
    int r = await t;  // 42
}
```

---

## 5. Classes and structs

### C++ class

```cpp
class Animal {
public:
    std::string name;
    Animal(const std::string& n) : name(n) {}
    virtual std::string speak() const { return "..."; }
    virtual ~Animal() = default;
};
class Dog : public Animal {
public:
    Dog() : Animal("dog") {}
    std::string speak() const override { return "woof"; }
};
```

### Rust struct + impl

```rust
trait Speak {
    fn speak(&self) -> String;
}
struct Animal { name: String }
impl Animal { fn new(name: &str) -> Self { Self { name: name.into() } } }
impl Speak for Animal {
    fn speak(&self) -> String { "...".into() }
}
struct Dog;
impl Dog { fn new() -> Self { Dog } }
impl Speak for Dog {
    fn speak(&self) -> String { "woof".into() }
}
```

### HP-HL class

```hp-hl
class Animal {
    public string Name;
    public Animal(string n) { Name = n; }
    public virtual string Speak() { return "..."; }
}
class Dog : Animal {
    public Dog() : base("dog") {}
    public override string Speak() { return "woof"; }
}
```

### Key differences

| Concept              | C++                          | Rust                          | HP-HL                            |
|----------------------|------------------------------|-------------------------------|----------------------------------|
| Single inheritance   | `class B : public A`         | single struct inheritance     | `class B : A`                    |
| Interfaces           | `class I { virtual ... };`   | `trait I { fn ...; }`         | `interface I { ... }`            |
| Virtual methods      | `virtual` + `override`       | all methods are virtual       | `virtual` + `override`           |
| Abstract             | `virtual ... = 0;`           | trait without default         | not yet (v0.95)                  |
| Field initialization | member initializer           | `#[derive(Default)]`          | inline or in constructor         |
| Destructor           | `~Class()` virtual           | `Drop` trait                  | GC; no manual destructor         |

---

## 6. Error handling

| Style              | C++                       | Rust               | HP-HL                       |
|--------------------|---------------------------|--------------------|-----------------------------|
| Exceptions         | `throw new Ex("...");`    | (libraries)        | `throw new Ex("...");`      |
| Result type        | `std::expected<T, E>`     | `Result<T, E>`     | `Result<T, E>` (planned)    |
| Error codes (out)  | `bool ok`                 | -                  | `out E error; return ok;`   |
| Panic              | `abort();`                | `panic!()`         | `panic("msg");`             |
| Catch              | `try { } catch(...)`      | `match Result`     | `try { } catch (E e) { }`   |

HP-HL has `try`/`catch`/`finally` like Java/C# and `panic` like Rust. A `Result` type is planned for v0.95.

---

## 7. Concurrency

### Threads

**C++** uses `std::thread` or `std::async`. **Rust** uses `std::thread::spawn`. **HP-HL** uses `spawn { ... }`:

```hp-hl
spawn { print("hello from another thread\n"); };
```

The closure runs on a worker thread from the runtime pool.

### Async/await

Already shown above. The `task<T>` type is roughly equivalent to Rust's `impl Future<Output=T>`.

### Channels

**Rust**

```rust
use std::sync::mpsc;
let (tx, rx) = mpsc::channel();
tx.send(42).unwrap();
let v = rx.recv().unwrap();
```

**HP-HL**

```hp-hl
channel<int> ch = new channel<int>(4);
spawn { ch.Send(42); };
int v = ch.Receive();
```

### Mutex

**Rust**

```rust
use std::sync::Mutex;
let m = Mutex::new(0);
*m.lock().unwrap() += 1;
```

**HP-HL**

```hp-hl
lock int m = 0;
lock (m) { m = m + 1; }
```

---

## 8. Pattern matching

**Rust**

```rust
match x {
    0 => println!("zero"),
    1..=9 => println!("one digit"),
    n if n < 0 => println!("negative"),
    _ => println!("other"),
}
```

**HP-HL**

```hp-hl
match (x) {
    0 => print("zero\n"),
    1..9 => print("one digit\n"),
    n if n < 0 => print("negative\n"),
    _ => print("other\n"),
}
```

`Option<T>` and tuple patterns are also supported.

---

## 9. Macros / generics

### C++ templates

```cpp
template <typename T>
T Max(T a, T b) { return a > b ? a : b; }
```

### Rust generics

```rust
fn max<T: PartialOrd>(a: T, b: T) -> T { if a > b { a } else { b } }
```

### HP-HL generics

```hp-hl
T Max<T>(T a, T b) where T : Comparable {
    return a > b ? a : b;
}
```

HP-HL uses **monomorphization** (one copy per concrete type) like C++ and Rust. Value parameters (`const int N`) are also supported:

```hp-hl
class Buffer<T, const int N> {
    T[N] data;
}
```

---

## 10. Modules and packages

| Concept              | C++                  | Rust                | HP-HL                            |
|----------------------|----------------------|---------------------|----------------------------------|
| File = module        | `#include "foo.h"`   | `mod foo;`          | `module myproj.foo;`             |
| Visibility           | `public:`            | `pub fn`            | `public`/`private`               |
| Package manifest     | `CMakeLists.txt`     | `Cargo.toml`        | `hphl.pkg.toml`                  |
| Dependency           | `find_package(Foo)`  | `[dependencies]`    | `dependencies = ["std = "0.93""]`|
| Build                | `cmake --build`      | `cargo build`       | `hphl build`                     |

---

## 11. Common gotchas

### C++ → HP-HL

1. **No manual `delete`.** HP-HL is GC-managed. Use `heap` for GC, `shared` for refcount.
2. **No header files.** Each `.hphl` is a module; `import foo;` instead of `#include`.
3. **No `auto` in module scope.** Use `var` only inside function bodies.
4. **No multiple inheritance.** Single class, multiple interfaces.
5. **No operator overloading** (planned for v0.95).
6. **No templates with default type arguments** (use overloads).
7. **`print` does not add a newline.** Include `\n` in your string.

### Rust → HP-HL

1. **No borrow checker.** HP-HL tracks moves but does not enforce borrow rules at compile time. Use `ref`/`out` parameters explicitly when needed.
2. **No `Result`-by-default.** Use exceptions or `out` parameters.
3. **No lifetimes.** HP-HL is GC; lifetimes are managed by the runtime.
4. **No `unsafe`.** All HP-HL code is memory-safe.
5. **`async` is not a trait.** Just use `async` keyword on a function.

### Both → HP-HL

1. **Memory policies are explicit.** The default is `stack`; if you wanted "GC heap", you must say `heap`.
2. **Single inheritance only.** Use interfaces for multiple behaviors.
3. **No header/source split.** Each file is a module; declarations and definitions are in the same file.
4. **No preprocessor.** Configuration is done via `hphl.pkg.toml` and build-time evaluation.

---

## 12. Cheat sheet

| C++                              | Rust                          | HP-HL                              |
|----------------------------------|-------------------------------|------------------------------------|
| `int main() { ... }`             | `fn main() { ... }`           | `void Main() { ... }`              |
| `std::cout << x;`                | `println!("{x}");`            | `print(x);` (no newline)           |
| `std::vector<int> v;`            | `let v: Vec<i32> = Vec::new()`| `list<int> v = new list<int>();`   |
| `v.push_back(1);`                | `v.push(1);`                  | `v.Add(1);`                        |
| `v.size()`                       | `v.len()`                     | `v.Length`                         |
| `for (auto& x : v)`              | `for x in &v`                 | `foreach (var x in v)`             |
| `std::map<K, V> m;`              | `let m: HashMap<K, V>`        | `map<K, V> m = new map<K, V>();`   |
| `m[k] = v;`                      | `m.insert(k, v);`             | `m.Put(k, v);`                     |
| `m.count(k)`                     | `m.contains_key(&k)`          | `m.Contains(k)`                    |
| `m.at(k)` (or `m[k]`)            | `m.get(&k)`                   | `m.Get(k)` (returns null if absent)|
| `std::shared_ptr<T> p;`          | `Rc<T>` / `Arc<T>`            | `shared T p = new shared T();`     |
| `std::make_shared<T>(args)`      | `Rc::new(T::new(args))`       | `new shared T(args)`               |
| `std::thread t([]{...}); t.join();` | `thread::spawn(\|\| {...}).join()` | `spawn { ... };` (auto-join on exit) |
| `std::async([]{...})`            | `tokio::spawn(...)`           | `spawn { ... };` (fire-and-forget) |
| `std::future<T>`                 | `impl Future<Output=T>`       | `task<T>`                          |
| `co_await fut;`                  | `fut.await`                   | `await fut;`                       |
| `std::mutex<T> m;`               | `Mutex<T>`                    | `lock T m;`                        |
| `m.lock(); ... m.unlock();`      | `let _g = m.lock().unwrap();` | `lock (m) { ... }`                 |
| `std::condition_variable`        | `Condvar`                    | `condvar T c;`                     |
| `std::atomic<int> a;`            | `AtomicI32`                   | `atomic int a;`                    |
| `throw Ex("oops");`              | `panic!("oops");`             | `throw new Ex("oops");` or `panic("oops");`|
| `try { } catch(...) {}`          | `match Result { ... }`        | `try { } catch (Ex e) { }`         |
| `const int X = 42;`              | `const X: i32 = 42;`          | `const int X = 42;`                |
| `static int X = 0;`              | `static X: i32 = 0;`          | `static int X = 0;` (in function)  |
| `thread_local int x;`            | `thread_local!`               | `threadlocal int x;`               |

---

## See also

- [Language Reference](reference/language.md) — all HP-HL keywords
- [Standard Library](stdlib/index.md) — built-in functions
- [Cookbook](cookbook/index.md) — common patterns
- [Tutorial](tutorial/30min.md) — 30-minute hands-on intro
