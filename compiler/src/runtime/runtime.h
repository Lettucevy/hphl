#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#endif

/* COMPAT-2 (Sprint 4): versão estável de ABI do runtime.
 * v1.0 = 100. Mudanças incompatíveis exigem bump do major (ex: 200 = v2.0).
 * O programa aborta na inicialização se a versão do runtime for diferente. */
#define HPHL_RUNTIME_ABI_VERSION 100

