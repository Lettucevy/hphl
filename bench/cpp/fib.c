#include <stdio.h>
#include <stdint.h>
#include <time.h>

int64_t clock_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int64_t Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

int main() {
    int64_t t0 = clock_ns();
    int64_t r = Fib(34);
    int64_t dt = clock_ns() - t0;
    printf("fib(34) = %lld\n", (long long)r);
    printf("%lld us (interno)\n", (long long)(dt / 1000));
    return 0;
}