#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>

int main() {
    struct timeval start, end;
    
    volatile int64_t soma = 0;
    int64_t i = 0;
    
    gettimeofday(&start, NULL);
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    gettimeofday(&end, NULL);
    
    double us = (end.tv_sec - start.tv_sec) * 1000000.0 + (end.tv_usec - start.tv_usec);
    printf("soma = %lld\n", (long long)soma);
    printf("%.0f us (interno)\n", us);
    return 0;
}