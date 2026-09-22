/* guard.c — malloc com paginas de guarda para caçar escrita OOB do
 * código gerado pelo self-host. Layout por alocação:
 *   [pagina guardada][dados ...][pagina guardada]
 * Underflow e overflow de qualquer bloco => access violation imediata. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Hdr {
    struct Hdr* next;
    struct Hdr* prev;
    size_t size;
    void* base;
    size_t total;
} Hdr;

static Hdr* g_live = NULL;

static LONG WINAPI guard_handler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode == 0xC0000005) {
        void* rip = (void*)e->ContextRecord->Rip;
        CONTEXT* c = e->ContextRecord;
        fprintf(stderr,
                "[GUARD] OOB! rip=%p off=0x%llx addr=%p rax=%p rcx=%p rdx=%p\n",
                rip, (void*)GetModuleHandleW(NULL),
                (unsigned long long)((char*)rip - (char*)GetModuleHandleW(NULL)),
                (void*)e->ExceptionRecord->ExceptionInformation[1],
                (void*)c->Rax, (void*)c->Rcx, (void*)c->Rdx);
        fflush(stderr);
        unsigned char* code = (unsigned char*)rip;
        fprintf(stderr, "[GUARD] bytes: %02x %02x %02x %02x %02x %02x\n",
                code[0], code[1], code[2], code[3], code[4], code[5]);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
