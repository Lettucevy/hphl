#include <stdio.h>

int fatorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}

int main() {
    printf("fatorial(12) = %d\n", fatorial(12));
    return 0;
}