// c_loop.c — loop puro de 40M iterações (custo base: fib/soma context)
#include <stdio.h>
int main(void) {
    long long acc = 0;
    for (long long i = 0; i < 40000000; i++) acc += i + 1;
    printf("acc = %lld\n", acc);
    return 0;
}