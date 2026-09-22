# HP-HL em 30 Minutos

Um tutorial prático que guia você pela linguagem em 5 lições rápidas. Ao final, você terá escrito código concorrente, com tipos estáticos seguros e gerenciado por GC.

> **Pré-requisitos:** HP-HL instalado (`hphlc` no `$PATH`), terminal e editor de texto. Alvos: Windows x64 (principal), Linux, macOS.

---

## Lição 1 — Olá, HP-HL (5 min)

Crie um arquivo `licao1.hphl` e escreva:

```hp-hl
module hello;

void Main() {
    print("Olá, mundo HP-HL!\n");
    int x = 42;
    print("x = "); print(x); print("\n");
}
```

Compile e execute:

```bash
hphlc licao1.hphl -o licao1 && ./licao1
```

**Saída esperada:**

```
Olá, mundo HP-HL!
x = 42
```

### O que você aprendeu
- Todo arquivo inicia com a declaração `module <nome>;`.
- Todo programa tem exatamente uma função `void Main()` (o ponto de entrada).
- `print(...)` escreve na saída padrão. Múltiplas chamadas podem ser encadeadas.
- O HP-HL é **estaticamente tipado** — `int` é um inteiro nativo de 64 bits.

---

## Lição 2 — Classes e Genéricos (7 min)

O HP-HL suporta classes com herança simples, interfaces e tipos genéricos:

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

---

## Lição 3 — Políticas de Memória (5 min)

O HP-HL oferece controle explícito sobre a vida útil e localização dos dados:

| Política | Ciclo de Vida | GC? | Thread-safe |
| :--- | :--- | :--- | :--- |
| `stack` | Escopo léxico (pilha) | Não | Sim |
| `heap` | Gerenciado pelo GC | Sim | Sim |
| `shared` | Contagem de referência atômica | Não | Sim |
| `arena` | Liberação em lote (por frame) | Não | Não |
| `lock` | GC + mutex reentrante | Sim | Sim |

---

## Lição 4 — Concorrência: canais, async/await, parallel (7 min)

O HP-HL possui três primitivas de concorrência nativas:
- **Canais** (`channel<T>`) — filas tipadas e thread-safe.
- **Async/await** — tarefas assíncronas baseadas em corrotinas.
- **Parallel** — laços paralelos distribuídos em pool de threads.

```hp-hl
module concurrency;

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
    return total;
}

void Main() {
    int total = SumSquares();
    print("Soma dos quadrados = "); print(total); print("\n");
}
```

---

## Próximos Passos
- [Referência da Linguagem](../reference/language.md) — palavras-chave e sintaxe
- [Biblioteca Padrão](../stdlib/index.md) — funções nativas
- [Guia de Migração](../migration.md) — portando código C++/Rust
- [Cookbook](../cookbook/index.md) — receitas práticas
