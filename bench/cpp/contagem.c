#include <stdio.h>
#include <stdint.h>
#include <time.h>

int64_t clock_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main() {
    int64_t t0 = clock_ns();
    int64_t i = 0;
    int64_t soma = 0;
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    int64_t dt = clock_ns() - t0;
    printf("soma = %lld\n", (long long)soma);
    printf("%lld us (interno)\n", (long long)(dt / 1000));
    return 0;
}