#include <stdio.h>
#include <time.h>

int main() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int n = 10000;
    int soma = 0;
    for (int i = 1; i <= n; i++) soma += i * i;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("soma_quadrados = %d, ms = %.3f\n", soma, ms);
    return 0;
}