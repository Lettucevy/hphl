#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
extern void* _sh_Lexer_Lexer(void*, void*);
extern void* _sh_Lexer_Next(void*);
extern void* _sh_Parser_Parser(void*, void*);
extern void* _sh_Parser_ParseProgram(void*);
extern void* _sh_Semantic_Semantic(void*);
extern long long _sh_Semantic_CheckProgram(void*, void*);
LONG WINAPI handler(EXCEPTION_POINTERS* e) {
    printf("[CRASH] code=%08lx\n", e->ExceptionRecord->ExceptionCode);
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
int probe(const char* tag) {
    void* p = malloc(32);
    if (!p) { printf("[%s HEAP-MORTO]\n", tag); return 1; }
    free(p);
    printf("[%s ok]\n", tag);
    return 0;
}
int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(handler);
    char* src = argv[1];
    int mode = argc > 2 ? argv[2][0] : '0';  /* l=lex, p=parse, s=semantica */
    void* lx = calloc(1, 32);
    _sh_Lexer_Lexer(lx, src);
    if (mode == 'l') {
        for (int i = 0; i < 500; i++) {
            void* t = _sh_Lexer_Next(lx);
            if (!t || *(long long*)t == 0) break;
        }
        if (probe("lex")) return 1;
        return 0;
    }
    void* pr = calloc(1, 64);
    _sh_Parser_Parser(pr, lx);
    void* prog = _sh_Parser_ParseProgram(pr);
    printf("[prog=%p]\n", prog);
    fflush(stdout);
    if (probe("parse")) return 1;
    if (prog && mode == 's') {
        void* sem = calloc(1, 65536);
        long long errs = _sh_Semantic_CheckProgram(sem, prog);
        printf("[errs=%lld]\n", errs);
        if (probe("sem")) return 1;
        return errs == 0 ? 0 : 2;
    }
    return prog ? 0 : 3;
}
