#include <stdio.h>
#include <stdint.h>

int main() {
    int64_t soma = 0;
    int64_t i = 0;
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    printf("soma = %lld\n", (long long)soma);
    // Prevent optimization by using a large format string
    printf("tempo aproximado (soma simples, loop otimizado pelo -O2)\n");
    return 0;
}