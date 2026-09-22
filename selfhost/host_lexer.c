#include <stdio.h>
#include <stdlib.h>
extern void* _sh_Lexer_Lexer(void*, void*);
extern void* _sh_Lexer_Next(void*);
int main(int argc, char** argv){
    void* lx = calloc(1, 32);
    _sh_Lexer_Lexer(lx, argc > 1 ? argv[1] : "int soma = a1 + 42;");
    for (;;) {
        void* tok = _sh_Lexer_Next(lx);
        long long kind = *(long long*)((char*)tok + 8);
        char* txt = *(char**)((char*)tok + 16);
        printf("[%lld] %s\n", kind, txt ? txt : "");
        if (kind == 0) break;
    }
    return 0;
}