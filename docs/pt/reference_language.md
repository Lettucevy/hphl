# Referência da Linguagem HP-HL

Este documento é a especificação canônica da linguagem de programação HP-HL. Cobre palavras-chave, tipos, expressões, instruções e arquitetura semântica.

---

## Sistema de Tipos

O HP-HL distingue tipos por valor (`struct`, tipos primitivos) e tipos por referência (`class`, arrays dinâmicos, strings):

### Tipos Primitivos

| Tipo | Tamanho | Descrição |
| :--- | :--- | :--- |
| `int` | 64 bits | Inteiro com sinal em complemento de dois |
| `uint` | 64 bits | Inteiro sem sinal |
| `float` | 64 bits | Ponto flutuante IEEE 754 de precisão dupla (f64) |
| `bool` | 8 bits | Valor booleano (`true` ou `false`) |
| `char` | 8 bits | Caractere ASCII / byte |
| `string` | Gerenciado | Sequência imutável codificada em UTF-8 |
| `ptr` | 64 bits | Ponteiro bruto para interoperabilidade FFI |

---

## Declaração de Tipos: Structs vs Classes

```hphl
// Struct alocada por valor sem rastreamento de GC
struct Point3D {
    float x;
    float y;
    float z;
}

// Classe gerenciada pelo Coletor de Lixo com suporte a herança
class Actor {
    public string Name;
    public Point3D Position;

    public Actor(string name, Point3D pos) {
        this.Name = name;
        this.Position = pos;
    }

    public virtual void Update(float deltaTime) {
        // Lógica de atualização
    }
}
```

---

## Controle de Fluxo

O HP-HL suporta estruturas familiares de controle:
- `if`, `else if`, `else`
- `while`, `do ... while`
- `for (init; cond; step)`
- `foreach (var item in collection)`
- `switch`, `case`, `default`
- `break`, `continue`, `return`

---

## Interoperabilidade Nativa FFI

Funções externas de bibliotecas dinâmicas C/C++ são declaradas com a cláusula `extern`:

```hphl
extern "kernel32" int GetTickCount();
extern "user32" int MessageBoxA(int hwnd, string text, string caption, int type);
```
