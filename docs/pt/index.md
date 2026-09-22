# Documentação Oficial do HP-HL

Bem-vindo à documentação oficial do **HP-HL (High-Performance High-Level Language)**.

O HP-HL é uma linguagem compilada e estaticamente tipada, desenvolvida especificamente para **motores de jogos, simulações físicas, computação gráfica, servidores de baixa latência e WebAssembly**, unindo a produtividade e expressividade das linguagens modernas com o controle e velocidade de execução de C e C++.

---

## Principais Recursos

- **Desempenho Bare-Metal C++:** Geração direta de código de máquina nativo x64 e assembly LLVM com autovetorização AVX2/SIMD. Em benchmarks numéricos e de matrizes, o HP-HL iguala ou supera C++ (GCC `-O3`).
- **Modelo Híbrido de Memória:**
  - `struct`: Alocação por valor na pilha e registradores, 0 bytes de sobrecarga de cabeçalho, cópia por valor e memória contígua e vetorizável.
  - `class`: Polimorfismo e alocação na heap gerenciada por um Coletor de Lixo geracional de baixa latência (Immix mark-sweep).
- **Concorrência Nativa de Primeira Classe:** Comunicação segura entre threads através de canais tipados (`channel<T>`), tarefas leves (`spawn`, `async`/`await`) e primitivas de sincronização direta (`mutex`, `atomic`).
- **FFI v2 com Sobrecarga Zero:** Ligação direta a bibliotecas dinâmicas do sistema operacional e C (`extern "user32"`, `extern "vulkan-1"`, `extern "sqlite3"`), manipulando ponteiros brutos (`ptr`), buffers e memória sem penalidades de conversão.
- **WebAssembly de Primeira Classe:** Compilação direta para `.wasm` via LLVM e `wasm-ld`, permitindo gráficos 3D de alta performance e computação WebGL/WebGPU no navegador.

---

## Navegação

| Seção | Descrição |
| :--- | :--- |
| [Instalação & SDK](installation.md) | Como baixar o SDK v1.0.0, configurar seu ambiente e usar o compilador. |
| [HP-HL em 30 Minutos](tutorial/30min.md) | Guia prático cobrindo sintaxe, tipos, classes, concorrência e compilação. |
| [Guia de Performance](performance_guide.md) | Análise aprofundada de `struct` vs `class`, layouts de memória e benchmarks vs C++. |
| [Motores de Jogos & Vulkan](vulkan_game_engines.md) | Como criar motores gráficos e simulações com Vulkan e FFI v2. |
| [WebAssembly & Web](webassembly.md) | Compilação com alvo em WebAssembly e integração com navegadores. |
| [Referência da Linguagem](reference/language.md) | Gramática completa, regras de sintaxe, palavras-chave e sistema de tipos. |
| [Referência FFI](reference/ffi.md) | Guia abrangente para integração com bibliotecas nativas e APIs do SO. |
| [Biblioteca Padrão](stdlib/index.md) | Módulos padrão (`std.io`, `std.math`, `std.collections`, `std.sync`). |
| [Cookbook](cookbook/index.md) | Padrões de design, receitas idiomáticas e exemplos práticos. |

---

## Primeiro Programa

```hphl
module main;

// Struct por valor: 0 bytes de sobrecarga, alocada na pilha
struct Vec3 {
    float x;
    float y;
    float z;
}

Vec3 Add(Vec3 a, Vec3 b) {
    Vec3 r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

void Main() {
    Vec3 p1; p1.x = 1.0; p1.y = 2.0; p1.z = 3.0;
    Vec3 p2; p2.x = 4.0; p2.y = 5.0; p2.z = 6.0;

    Vec3 p3 = Add(p1, p2);

    print("Posição final: (");
    print(p3.x); print(", ");
    print(p3.y); print(", ");
    print(p3.z); print(")\n");
}
```

Para compilar e executar:

```bash
hphlc main.hphl -o main.exe
./main.exe
```

Saída:

```text
Posição final: (5, 7, 9)
```

---

## Download do SDK

Visite a página de [Instalação & Download do SDK](installation.md) para obter pacotes oficiais para Windows, Linux e macOS.
