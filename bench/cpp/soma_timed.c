#include <stdio.h>
#include <time.h>
#include <stdint.h>

int main() {
    clock_t start = clock();
    int64_t soma = 0;
    int64_t i = 0;
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    clock_t end = clock();
    double ms = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;
    printf("soma = %lld\n", (long long)soma);
    printf("%.0f us (interno)\n", ms * 1000.0);
    return 0;
}