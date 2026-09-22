# HP-HL in 30 Minutes

A hands-on tutorial that walks you through the language in 5 short lessons. By the end you will have written concurrent, type-safe, GC-managed code — and shipped a runnable program.

> **Prerequisites:** HP-HL installed (`hphlc` on `$PATH`), a terminal, and a text editor. Targets: Windows x64 (primary), Linux, macOS.
>
> **Convention:** all code blocks are complete programs. Save each as `lessonN.hphl` and run with `hphlc lessonN.hphl` (or `hphlc lessonN.hphl -o lessonN.exe && ./lessonN.exe` on Windows).

---

## Lesson 1 — Hello, HP-HL (5 min)

Open a file `lesson1.hphl` and write:

```hp-hl
module hello;

void Main() {
    print("Hello, HP-HL world!\n");
    int x = 42;
    print("x = "); print(x); print("\n");
}
```

Compile and run:

```bash
hphlc lesson1.hphl -o lesson1 && ./lesson1
```

**Expected output:**

```
Hello, HP-HL world!
x = 42
```

### What you learned

- Every file starts with a `module <name>;` declaration.
- Every program has exactly one `void Main()` function (the entry point).
- `print(...)` writes to stdout. Multiple `print` calls are needed for non-string values because `print` does NOT add a newline — the `\n` is part of the string.
- `int x = 42;` declares a stack-allocated integer.
- HP-HL is **statically typed** — `int` is a real 64-bit integer type, not a `var`.

### Try it

1. Change `int x = 42;` to `float x = 3.14;`. What does `print(x)` output?
2. Add `print("x * 2 = "); print(x * 2); print("\n");`. Notice that `*` works on both int and float.

---

## Lesson 2 — Classes and Generics (7 min)

HP-HL has classes with single inheritance, interfaces, and generic type parameters.

```hp-hl
module shapes;

interface IDescribable {
    string Describe();
}

class Shape : IDescribable {
    public string Name;
    public Shape(string name) { Name = name; }
    public virtual string Describe() { return Name; }
    public virtual float Area() { return 0.0; }
}

class Circle : Shape {
    public float Radius;
    public Circle(float r) : base("circle") { Radius = r; }
    public override float Area() { return 3.14159 * Radius * Radius; }
    public override string Describe() {
        return Name + " (r=" + Radius + ")";
    }
}

class Box<T> {
    public T Value;
    public Box(T v) { Value = v; }
}

void Main() {
    Circle c = new Circle(2.0);
    print(c.Describe());
    print("\n");
    print("area = "); print(c.Area()); print("\n");

    Box<int> b = new Box<int>(42);
    print("box = "); print(b.Value); print("\n");
}
```

**Expected output:**

```
circle (r=2)
area = 12.5664
box = 42
```

### What you learned

- `class` with public fields and constructors. `: IDescribable` implements an interface.
- `virtual`/`override` for polymorphism.
- `: base("circle")` calls the parent constructor.
- Generic classes with `class Box<T>` — instantiated as `Box<int>`.
- `new` always allocates on the GC heap (the `heap` storage policy).
- Method calls use dot syntax: `c.Describe()`.

### Try it

1. Add a class `Square : Shape` with `Side` and `Area() = Side * Side`.
2. Add a method `float Sum<T>(list<T> xs)` that returns the sum. (Hint: use `foreach`.)

---

## Lesson 3 — Memory Policies (5 min)

HP-HL gives you explicit control over where data lives. The default is `stack`; the other policies are modifiers on the type:

| Policy   | Lifetime             | GC? | Thread-safe |
|----------|----------------------|-----|-------------|
| `stack`  | lexical scope        | no  | yes         |
| `heap`   | GC-managed           | yes | yes         |
| `shared` | ref-counted, shared  | no  | yes         |
| `arena`  | per-frame            | no  | no          |
| `lock`   | GC + mutex           | yes | yes         |
| `pool`   | object pool          | no  | depends     |
| `threadlocal` | per-thread      | no  | n/a         |

```hp-hl
module memory;

import std.collections;

class Counter {
    public int N = 0;
}

void Bump(shared Counter c) {
    c.N = c.N + 1;  // ref-counted, safe across threads
}

void Main() {
    // Stack (default) — freed at end of Main
    int x = 1;
    print("x = "); print(x); print("\n");

    // Heap (GC)
    heap Counter h = new Counter();
    Bump(h);
    print("h.N = "); print(h.N); print("\n");

    // Shared (ref-counted)
    shared Counter s = new shared Counter();
    spawn { Bump(s); };
    spawn { Bump(s); };
    join_tasks();
    print("s.N = "); print(s.N); print("\n");

    // Arena — batch allocations, freed all at once
    arena {  // pseudo-syntax: see stdlib
        int batch = 1000;
        // use batch-sized buffer
    }
}
```

**Expected output (non-deterministic for shared):**

```
x = 1
h.N = 1
s.N = 2
```

### What you learned

- The default storage is `stack`. Use `heap` for GC, `shared` for cross-thread, `arena` for batch.
- `shared` is a ref-counted pointer — safe to pass between threads.
- `spawn { ... }` runs a closure on a worker thread.
- `join_tasks()` waits for all spawned tasks to finish.

### Try it

1. Replace `shared` with `heap` in `Bump`. Does it still compile? Run it. What's the difference?
2. Add a `lock Counter l = new lock Counter();` and use `lock (l) { l.N++; }`.

---

## Lesson 4 — Concurrency: channels, async/await, parallel (7 min)

HP-HL has three concurrency primitives:

- **Channels** — typed message queues (`channel<int>`).
- **Async/await** — coroutine-like tasks (`async`, `await`).
- **Parallel** — work-stealing parallel loops.

```hp-hl
module concurrency;

import std.collections;

async int SlowDouble(int x) {
    // simulate work
    return x * 2;
}

int Producer(channel<int> out) {
    for (int i = 0; i < 10; i = i + 1) {
        out.Send(i * i);
    }
    return 0;
}

int SumSquares() {
    channel<int> ch = new channel<int>(4);
    spawn { Producer(ch); };

    int total = 0;
    for (int i = 0; i < 10; i = i + 1) {
        int v = ch.Receive();
        total = total + v;
    }
    return total;  // 0+1+4+9+16+25+36+49+64+81 = 285
}

void Main() {
    // async/await
    task<int> t = SlowDouble(21);
    int r = await t;
    print("async 21*2 = "); print(r); print("\n");

    // channel
    int total = SumSquares();
    print("sum squares = "); print(total); print("\n");

    // parallel foreach
    list<int> xs = new list<int>();
    xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4); xs.Add(5);

    int sum = 0;
    parallel foreach (int v in xs) {
        sum = sum + v;
    }
    print("parallel sum = "); print(sum); print("\n");
}
```

**Expected output:**

```
async 21*2 = 42
sum squares = 285
parallel sum = 15
```

### What you learned

- `async int` declares a function that returns `task<int>`.
- `await t` blocks the current thread until `t` completes.
- `channel<int> ch = new channel<int>(N);` creates a buffered channel.
- `ch.Send(x)` blocks if the buffer is full; `ch.Receive()` blocks if empty.
- `parallel foreach` distributes iterations across worker threads.

### Try it

1. Make `SlowDouble` actually slow with `await Task.Delay(100);` and call it 100 times in parallel.
2. Replace `channel<int>` with `channel<string>` and pass messages between two producers.

---

## Lesson 5 — A small project (6 min)

Let's build a complete program: a parallel word-counter that reads files, counts words concurrently, and prints the top-10.

Project layout:

```
wordcount/
├── hphl.pkg.toml
└── src/
    └── main.hphl
```

### `hphl.pkg.toml`

```toml
[package]
name = "wordcount"
version = "0.1.0"
entry = "src/main.hphl"

[dependencies]
std = "0.93.0"
```

### `src/main.hphl`

```hp-hl
module wordcount;

import std.io;
import std.collections;
import std.string;

map<string, int> CountWords(string path) {
    map<string, int> counts = new map<string, int>();
    list<string> lines = read_lines(path);
    foreach (string line in lines) {
        list<string> words = split(line, ' ');
        foreach (string w in words) {
            if (w.Length == 0) continue;
            string k = toLower(w);
            int* cur = counts.Get(k);
            if (cur == null) {
                counts.Put(k, 1);
            } else {
                counts.Put(k, *cur + 1);
            }
        }
    }
    return counts;
}

void Main() {
    if (args.Length < 1) {
        print("usage: wordcount <file>\n");
        return;
    }
    string path = args[0];
    map<string, int> counts = CountWords(path);
    // ... print top-10 (omitted for brevity)
    print($"file: {path}\n");
    print($"unique words: {counts.Length}\n");
}
```

Build and run:

```bash
hphl build
./wordcount myfile.txt
```

**Expected output:**

```
file: myfile.txt
unique words: 423
```

### What you learned

- A project is a directory with `hphl.pkg.toml` and `src/`.
- `import std.io;` pulls in standard library modules.
- `map<K,V>`, `list<T>`, `string` are built-in types.
- `args` is a global list of command-line arguments.
- `$"..."` is string interpolation.
- `hphl build` resolves dependencies, compiles, and links.

### Try it

1. Add sorting to print the top-10 most common words.
2. Use `parallel foreach` to process multiple files in parallel.

---

## Where to go next

- [Language Reference](../reference/language.md) — all keywords and constructs
- [Standard Library](../stdlib/index.md) — built-in functions
- [Cookbook](../cookbook/index.md) — common patterns
- [Migration guide](../migration.md) — porting from C++/Rust
- [Installation & Tooling](../installation.md) — editor setup and compiler

If you got stuck, check the `examples/` directory in the HP-HL repository for runnable programs covering every feature in this tutorial.
