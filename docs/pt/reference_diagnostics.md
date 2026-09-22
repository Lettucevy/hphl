# Diagnósticos e Erros do Compilador

O compilador HP-HL (`hphlc`) fornece mensagens diagnósticas precisas inspiradas na clareza dos compiladores modernos, exibindo trechos de código destacados, linhas e colunas exatas.

---

## Categorias de Erros

| Código | Descrição |
| :--- | :--- |
| `E001` | Símbolo ou identificador não declarado no escopo |
| `E002` | Incompatibilidade de tipos na atribuição ou retorno |
| `E003` | Construtor inexistente ou argumentos incompatíveis |
| `E004` | Violação de herança ou tentativa de sobreposição de método não virtual |
| `E005` | Parâmetros inválidos na cláusula `extern` do FFI |
| `E006` | Acesso ilegal a membros privados ou protegidos |

---

## Exemplos de Mensagens

```text
error[E002]: type mismatch in assignment
  --> src/main.hphl:14:9
   |
14 |     int count = "42";
   |         ^^^^^   ^^^^ expected 'int', found 'string'
   |
   = help: use parse_int(str) to convert a string to an integer
```
