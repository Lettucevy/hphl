// c_fib.c — mesmo algoritmo de bench_fib.hphl (fib(34) ingênuo, 64 bits)
#include <stdio.h>
long long fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(void) {
    long long r = fib(34);
    printf("fib(34) = %lld\n", r);
    return 0;
}