# Guia de Migração C++ & Rust para HP-HL

Este guia auxilia desenvolvedores experientes em C, C++, C# e Rust a transitar seus códigos e padrões de arquitetura para o HP-HL.

---

## Comparativo de Conceitos

| C++ | Rust | HP-HL |
| :--- | :--- | :--- |
| `struct` / `class` por valor | `struct` | `struct` (por valor, 0 bytes overhead) |
| `std::shared_ptr<T>` | `Arc<T>` | `shared T` (contagem de referência) |
| `new T` / `std::unique_ptr` | `Box<T>` | `class T` (alocado no GC Immix) |
| `std::thread` | `std::thread::spawn` | `spawn { ... }` (thread pool nativo) |
| `std::vector<T>` | `Vec<T>` | `list<T>` |
| `std::unordered_map` | `HashMap<K, V>` | `map<K, V>` |

---

## Portando Funções e Ponteiros

No HP-HL, chamadas diretas da API do sistema não requerem arquivos de cabeçalho (`.h`); basta declarar a função desejada com `extern "<biblioteca>"`.
