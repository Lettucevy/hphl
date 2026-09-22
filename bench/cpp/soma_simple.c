#include <stdio.h>

int main() {
    long long soma = 0;
    long long i = 0;
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    printf("soma = %lld\n", soma);
    return 0;
}