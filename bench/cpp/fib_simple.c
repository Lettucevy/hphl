#include <stdio.h>
#include <stdint.h>

int64_t Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

int main() {
    long long r = Fib(34);
    printf("fib(34) = %lld\n", r);
    return 0;
}