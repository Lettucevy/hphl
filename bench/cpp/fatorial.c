#include <stdio.h>
#include <stdint.h>
#include <time.h>

int64_t fatorial(int n) {
    int64_t result = 1;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}

int main() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int n = 12;
    int64_t result = fatorial(n);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("fatorial(%d) = %ld, ms = %.3f\n", n, result, ms);
    return 0;
}
