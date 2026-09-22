# Referência do FFI v2 Nativo

O subsistema Foreign Function Interface (FFI) do HP-HL permite vincular e invocar bibliotecas nativas de C, Win32, POSIX, Vulkan e DirectX diretamente a partir do código HP-HL, sem necessidade de escrever wrappers intermediários em C.

---

## Sintaxe da Cláusula `extern`

```hphl
module app.native;

// Vinculação estática com DLL/so do sistema
extern "user32" int MessageBoxA(int hwnd, string text, string caption, int type);
extern "kernel32" int GetTickCount();
```

---

## Passagem de Ponteiros Brutos (`ptr`)

Para manipular estruturas em C ou buffers alocados pelo sistema:

```hphl
extern "vulkan-1" int vkCreateInstance(ptr pCreateInfo, ptr pAllocator, ptr pInstance);
```

Funções utilitárias como `hphl_mem_copy`, `hphl_mem_peek_*` e `hphl_mem_poke_*` operam diretamente sobre ponteiros brutos para máxima velocidade de transferência.
