#include <stdio.h>
#include <time.h>
#include <stdint.h>

int64_t Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

int main() {
    clock_t start = clock();
    int64_t r = Fib(34);
    clock_t end = clock();
    double ms = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;
    printf("fib(34) = %lld\n", (long long)r);
    printf("%.0f us (interno)\n", ms * 1000.0);
    return 0;
}