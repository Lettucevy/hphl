# Modelo de Memória & Políticas de Alocação

O HP-HL adota uma arquitetura de gerenciamento híbrido de memória que combina velocidade de execução comparável a C++ com a conveniência de coleta automática de lixo.

---

## Arquitetura de Memória

1. **Memória de Pilha (Stack):** Usada para tipos primitivos e `struct`. Alocação instantânea com liberação automática ao final do escopo léxico, com zero sobrecarga de GC.
2. **Coletor de Lixo Geracional (Immix GC):** Gerencia objetos `class` dinâmicos. Emprega um nursery jovem para alocação ultrarrápida e um heap maduro com tabelas de cartões (card tables) e marcação precisa.
3. **Arenas de Alocação em Lote:** Permitem que subsistemas (como simulação de partículas ou renderização por quadro) aloquem múltiplos blocos contíguos e liberem toda a memória em uma única operação $O(1)$.
4. **Ponteiros Compartilhados (`shared`):** Contagem atômica de referências thread-safe sem necessidade de varredura global do coletor.

---

## Barreiras de Escrita (Write Barriers)

O compilador insere barreiras de escrita geracionais otimizadas sempre que uma referência de um objeto da geração antiga aponta para um objeto do nursery jovem:

```c
void hphl_write_barrier(void* old_obj, void* field_addr);
```

Isto garante pausas de coleta mínimas (tipicamente sub-milissegundo).
