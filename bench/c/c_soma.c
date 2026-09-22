// c_soma.c — mesmo algoritmo de bench_soma.hphl: 20M iterações de soma
#include <stdio.h>
int main(void) {
    long long soma = 0;
    for (long long i = 0; i < 20000000; i++) soma += i + 1;
    printf("soma = %lld\n", soma);
    return 0;
}