# Visão Geral da Biblioteca Padrão (Stdlib)

A biblioteca padrão do HP-HL oferece um conjunto abrangente de tipos, funções e utilitários integrados de alta performance.

---

## Módulos Principais

- **`std.math`**: Funções matemáticas de alta precisão (`abs`, `sqrt`, `min`, `max`, `sin`, `cos`, `pow`, `PI`, `E`).
- **`std.io`**: Entrada e saída de arquivos e console (`read_file`, `write_file`, `append_file`, `read_line`, `list_dir`).
- **`std.collections`**: Coleções dinâmicas de alta performance (`list<T>`, `map<K, V>`).
- **`std.sync`**: Primitivas de concorrência (`channel<T>`, `mutex`, `semaphore`, `barrier`, `event`).
- **`std.json`**: Serialização e análise ultra-rápida de documentos JSON.

Consulte a [Referência da API C do Runtime](runtime.md) para detalhes de baixo nível sobre cada função do runtime.
