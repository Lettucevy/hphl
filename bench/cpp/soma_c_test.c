#include <stdio.h>
#include <stdint.h>
#include <time.h>

int64_t soma_loop(int n) {
    int64_t soma = 0;
    for (int i = 1; i <= n; i++) soma += i;
    return soma;
}

int main() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int n = 20000000;
    int64_t result = soma_loop(n);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("soma = %ld, ms = %.3f\n", result, ms);
    return 0;
}