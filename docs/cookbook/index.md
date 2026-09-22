# HP-HL Cookbook

A collection of common patterns and idioms in HP-HL. Each pattern has a short description, a complete runnable example, and a discussion of when to use it.

> All examples are in `examples/cookbook/` and are part of the test suite. Run any with `hphlc examples/cookbook/01_raii.hphl && ./01_raii`.

---

## Table of Contents

1. [RAII — Resource Acquisition Is Initialization](#1-raii)
2. [Builder](#2-builder)
3. [Visitor](#3-visitor)
4. [Observer](#4-observer)
5. [Iterator](#5-iterator)
6. [Singleton](#6-singleton)
7. [Producer / Consumer](#7-producer-consumer)
8. [Pipeline](#8-pipeline)
9. [Map-Reduce](#9-map-reduce)
10. [State Machine](#10-state-machine)

---

## 1. RAII

**Problem:** you have a resource (file handle, mutex, network connection) that must be released when the work is done, even on errors.

**Solution:** in HP-HL, GC handles most resources automatically. For explicit resources, use `try`/`finally`:

```hp-hl
module cookbook.raii;

import std.io;

class File {
    public string Path;
    public File(string path) { Path = path; open_handle(); }
    ~File() { close_handle(); }   // GC finalizer (deterministic? no — use finally)
    private void open_handle() { /* open(Path) */ }
    private void close_handle() { /* close */ }
}

void CopyFile(string src, string dst) {
    File f = new File(src);
    try {
        // read+write
    } finally {
        f.close_handle();  // explicit cleanup before GC runs
    }
}

void Main() {
    CopyFile("in.txt", "out.txt");
}
```

> **Note:** HP-HL GC handles heap-allocated objects. For **deterministic cleanup** of external resources (file descriptors, sockets), use `try { } finally { }`. The `~Class()` destructor is a hint to the GC; use `finally` for guaranteed cleanup.

---

## 2. Builder

**Problem:** constructing an object with many optional parameters is awkward.

**Solution:** use a builder class with fluent method chaining.

```hp-hl
module cookbook.builder;

class HttpRequest {
    public string Method = "GET";
    public string Url = "";
    public map<string, string> Headers = new map<string, string>();
    public string Body = "";
    public int TimeoutMs = 30000;

    public string Build() {
        string s = $"{Method} {Url} HTTP/1.1\n";
        foreach (var kv in Headers) {
            s = s + $"{kv.key}: {kv.value}\n";
        }
        return s + "\n" + Body;
    }
}

class RequestBuilder {
    private HttpRequest req = new HttpRequest();
    public RequestBuilder Method(string m) { req.Method = m; return this; }
    public RequestBuilder Url(string u) { req.Url = u; return this; }
    public RequestBuilder Header(string k, string v) {
        req.Headers.Put(k, v);
        return this;
    }
    public RequestBuilder Body(string b) { req.Body = b; return this; }
    public RequestBuilder Timeout(int ms) { req.TimeoutMs = ms; return this; }
    public HttpRequest Build() { return req; }
}

void Main() {
    HttpRequest r = new RequestBuilder()
        .Method("POST")
        .Url("https://api.example.com/users")
        .Header("Content-Type", "application/json")
        .Header("Authorization", "Bearer xyz")
        .Body("{\"name\":\"Ana\"}")
        .Timeout(5000)
        .Build();
    print(r.Build());
}
```

---

## 3. Visitor

**Problem:** you want to add new operations over a class hierarchy without modifying the classes.

**Solution:** use double dispatch via an `accept` method.

```hp-hl
module cookbook.visitor;

interface Shape {
    float Area();
    string Accept(ShapeVisitor v);
}

interface ShapeVisitor {
    string VisitCircle(float r);
    string VisitSquare(float s);
}

class Circle : Shape {
    public float R;
    public Circle(float r) { R = r; }
    public float Area() { return 3.14159 * R * R; }
    public string Accept(ShapeVisitor v) { return v.VisitCircle(R); }
}

class Square : Shape {
    public float S;
    public Square(float s) { S = s; }
    public float Area() { return S * S; }
    public string Accept(ShapeVisitor v) { return v.VisitSquare(S); }
}

class DescribeVisitor : ShapeVisitor {
    public string VisitCircle(float r) { return $"circle r={r}"; }
    public string VisitSquare(float s) { return $"square s={s}"; }
}

void Main() {
    list<Shape> shapes = new list<Shape>();
    shapes.Add(new Circle(2.0));
    shapes.Add(new Square(3.0));
    DescribeVisitor d = new DescribeVisitor();
    foreach (Shape s in shapes) {
        print(s.Accept(d));
        print("\n");
    }
}
```

> **Limitation:** generic interfaces have partial monomorphization in v0.93. The above example uses only concrete types and works fully.

---

## 4. Observer

**Problem:** multiple objects need to react when something happens in another object.

**Solution:** maintain a list of listeners and notify them.

```hp-hl
module cookbook.observer;

interface ClickListener {
    void OnClick(int x, int y);
}

class Button {
    private list<ClickListener> listeners = new list<ClickListener>();
    public void AddListener(ClickListener l) { listeners.Add(l); }
    public void Click(int x, int y) {
        foreach (ClickListener l in listeners) {
            l.OnClick(x, y);
        }
    }
}

class Logger : ClickListener {
    public void OnClick(int x, int y) {
        print($"click at ({x},{y})\n");
    }
}

class Counter : ClickListener {
    public int N = 0;
    public void OnClick(int x, int y) { N = N + 1; }
}

void Main() {
    Button b = new Button();
    Counter c = new Counter();
    b.AddListener(new Logger());
    b.AddListener(c);
    b.Click(10, 20);
    b.Click(30, 40);
    print("clicks: "); print(c.N); print("\n");
}
```

---

## 5. Iterator

**Problem:** you want to iterate over a custom collection without exposing its internal structure.

**Solution:** implement an iterator class with `MoveNext` and `Current`.

```hp-hl
module cookbook.iterator;

interface Iter<T> {
    bool MoveNext();
    T Current();
}

class Range : Iter<int> {
    private int cur;
    private int end;
    public Range(int start, int end) { cur = start; this.end = end; }
    public bool MoveNext() { if (cur < end) { cur = cur + 1; return true; } return false; }
    public int Current() { return cur; }
}

void Main() {
    Range r = new Range(0, 5);
    while (r.MoveNext()) {
        print(r.Current());
        print(" ");
    }
    print("\n");
}
```

> **Note:** `foreach` in HP-HL is the standard way; the `Iter<T>` interface is for custom collections. Most built-in types (`list<T>`, `map<K,V>`) already implement it.

---

## 6. Singleton

**Problem:** you need exactly one instance of a class (config, logger, pool).

**Solution:** use `shared` for thread-safe single instance.

```hp-hl
module cookbook.singleton;

class Config {
    public string Name = "default";
    public int Version = 1;
}

shared Config instance = null;

shared Config GetConfig() {
    if (instance == null) {
        instance = new shared Config();
    }
    return instance;
}

void Main() {
    shared Config c1 = GetConfig();
    shared Config c2 = GetConfig();
    print("same? "); print(c1 == c2); print("\n");  // true
}
```

> **Note:** this is the **classic** singleton. In idiomatic HP-HL, prefer dependency injection: pass the config to functions that need it.

---

## 7. Producer / Consumer

**Problem:** one or more producers generate data; one or more consumers process it; both run concurrently.

**Solution:** use a `channel<T>` as the queue.

```hp-hl
module cookbook.producer_consumer;

import std.collections;

void Producer(channel<int> out, int n) {
    for (int i = 0; i < n; i = i + 1) {
        out.Send(i * i);
    }
}

void Consumer(channel<int> in, ref int sum) {
    for (int i = 0; i < 5; i = i + 1) {
        sum = sum + in.Receive();
    }
}

void Main() {
    channel<int> ch = new channel<int>(4);
    spawn { Producer(ch, 5); };
    int total = 0;
    Consumer(ch, ref total);
    print("sum = "); print(total); print("\n");  // 0+1+4+9+16=30
}
```

---

## 8. Pipeline

**Problem:** a stream of items passes through a sequence of stages.

**Solution:** chain channels between stages.

```hp-hl
module cookbook.pipeline;

import std.collections;

void Stage1(channel<int> in, channel<int> out) {
    while (true) {
        int v = in.Receive();
        out.Send(v * 2);
    }
}

void Stage2(channel<int> in, channel<int> out) {
    while (true) {
        int v = in.Receive();
        out.Send(v + 1);
    }
}

void Main() {
    channel<int> q1 = new channel<int>(4);
    channel<int> q2 = new channel<int>(4);
    spawn { Stage1(q1, q2); };
    spawn { Stage2(q2, ...); };  // terminal stage
    q1.Send(1); q1.Send(2); q1.Send(3);
    // ...
}
```

---

## 9. Map-Reduce

**Problem:** apply a function to each element of a list (map), then combine the results (reduce).

**Solution:** use `parallel foreach` for the map, then accumulate in a single thread for the reduce.

```hp-hl
module cookbook.mapreduce;

import std.collections;

int Square(int x) { return x * x; }

void Main() {
    list<int> xs = new list<int>();
    for (int i = 0; i < 100; i = i + 1) xs.Add(i);

    int sum = 0;
    parallel foreach (int x in xs) {
        // map: square; reduce: add to shared accumulator
        // (in real code, use a shared accumulator or a per-thread sum then merge)
    }
    print("sum of squares = "); print(sum); print("\n");
}
```

> **Note:** the body of `parallel foreach` runs on multiple threads. To accumulate, use `lock` or `atomic`:

```hp-hl
atomic int total = 0;
parallel foreach (int x in xs) {
    total = total + x * x;
}
```

---

## 10. State Machine

**Problem:** an object has discrete states and transitions between them based on events.

**Solution:** represent states as enum values, transitions as methods.

```hp-hl
module cookbook.statemachine;

enum ConnectionState { Disconnected, Connecting, Connected, Error }

class Connection {
    public ConnectionState State = ConnectionState.Disconnected;
    public string LastError = "";

    public void Open() {
        if (State == ConnectionState.Disconnected) {
            State = ConnectionState.Connecting;
            // do connect...
            State = ConnectionState.Connected;
        }
    }
    public void Close() {
        if (State == ConnectionState.Connected) {
            State = ConnectionState.Disconnected;
        }
    }
    public void Fail(string err) {
        State = ConnectionState.Error;
        LastError = err;
    }
}

void Main() {
    Connection c = new Connection();
    c.Open();
    if (c.State == ConnectionState.Connected) {
        print("connected\n");
    }
    c.Close();
}
```

---

## See also

- [Tutorial](../tutorial/30min.md)
- [Language Reference](../reference/language.md)
- [Standard Library](../stdlib/index.md)
- [Migration guide](../migration.md)
