#include <stdio.h>
#include <windows.h>
#include <stdint.h>

int64_t Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

int main() {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    int64_t r = Fib(34);
    QueryPerformanceCounter(&end);
    double us = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart * 1000000.0;
    printf("fib(34) = %lld\n", (long long)r);
    printf("%.0f us (interno)\n", us);
    return 0;
}