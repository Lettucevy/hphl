# Livro de Receitas (Cookbook) HP-HL

Exemplos práticos e soluções idiomáticas para desafios comuns de programação de sistemas e jogos no HP-HL.

---

## 1. Processamento Paralelo com Canais (Produtor / Consumidor)

```hphl
module cookbook.producer_consumer;

void Producer(channel<int> queue) {
    for (int i = 1; i <= 100; i++) {
        queue.Send(i * i);
    }
}

void Main() {
    var queue = new channel<int>(16);
    spawn Producer(queue);

    for (int i = 1; i <= 100; i++) {
        int result = queue.Receive();
        print("Recebido: "); print(result); print("\n");
    }
}
```

---

## 2. Leitura e Gravação de Arquivos

```hphl
module cookbook.file_io;

void Main() {
    string path = "config.txt";
    write_file(path, "resolucao=1920x1080\nvsync=true\n");

    if (file_exists(path)) {
        string content = read_file(path);
        print("Conteúdo lido:\n");
        print(content);
    }
}
```
