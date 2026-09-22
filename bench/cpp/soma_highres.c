#include <stdio.h>
#include <windows.h>
#include <stdint.h>

int main() {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    int64_t soma = 0;
    int64_t i = 0;
    while (i < 20000000) {
        soma = soma + i;
        i++;
    }
    
    QueryPerformanceCounter(&end);
    double us = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart * 1000000.0;
    printf("soma = %lld\n", (long long)soma);
    printf("%.0f us (interno)\n", us);
    return 0;
}