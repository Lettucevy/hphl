#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>

int64_t Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

int main() {
    struct timeval start, end;
    gettimeofday(&start, NULL);
    volatile int64_t r = Fib(34);
    gettimeofday(&end, NULL);
    double us = (end.tv_sec - start.tv_sec) * 1000000.0 + (end.tv_usec - start.tv_usec);
    printf("fib(34) = %lld\n", (long long)r);
    printf("%.0f us (interno)\n", us);
    return 0;
}