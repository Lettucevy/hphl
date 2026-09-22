// c_parallel.c — redução por faixas com Win32 threads (mesmo algoritmo de
// bench_scaling.hphl): 4 threads, cada uma soma 1M de um array de 4M.
#include <stdio.h>
#include <windows.h>

#define N 4000000
long long arr[N];
long long parcial[256];

DWORD WINAPI worker(LPVOID p) {
    long long slot = (long long)p;
    long long lo = slot * 1000000;
    long long s = 0;
    for (long long i = lo; i < lo + 1000000; i++) s += arr[i];
    parcial[(size_t)slot] = s;
    return 0;
}

int main(void) {
    for (long long i = 0; i < N; i++) arr[i] = i + 1;
    HANDLE h[4];
    for (int k = 0; k < 4; k++) h[k] = CreateThread(NULL, 0, worker, (LPVOID)k, 0, NULL);
    WaitForMultipleObjects(4, h, TRUE, INFINITE);
    long long total = 0;
    for (int i = 0; i < 4; i++) total += parcial[i];
    printf("reduction = %lld\n", total);
    return 0;
}