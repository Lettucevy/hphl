# Motores de Jogos 3D & Vulkan

O HP-HL foi projetado para atender aos requisitos rigorosos do desenvolvimento de motores de jogos de alto desempenho modernos.

---

## Integração com a API Vulkan

A API Vulkan exige controle estrito sobre alinhamento de memória e chamadas diretas de C sem sobrecarga. O HP-HL oferece ponteiros brutos `ptr` e tipos `struct` por valor que correspondem exatamente à ABI dos cabeçalhos da Khronos:

```hphl
module graphics.vulkan;

extern "vulkan-1" {
    int vkCreateInstance(ptr pCreateInfo, ptr pAllocator, ptr pInstance);
    void vkDestroyInstance(ptr instance, ptr pAllocator);
}
```

---

## Gerenciamento de Recursos Gráficos

- **Vértices e Shaders:** Buffers de vértices e matrizes de projeção/visualização utilizam `struct` planas sem overhead.
- **Pipelines Concorrentes:** Utilize tarefas assíncronas (`spawn`) para carregar texturas, compilar shaders SPIR-V e processar física enquanto a thread de renderização submete listas de comandos Vulkan.
