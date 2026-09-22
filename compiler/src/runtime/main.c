/* HP-HL Runtime - modular entry point
 * This file includes all other runtime modules to maintain the existing
 * internal/static state and ABI while being organized by subsystem.
 */
#include "runtime_api.h"

#include "net/socket.c"
#include "core/core.c"
#include "strings/strings.c"
#include "concurrency/concurrency.c"
#include "collections/collections.c"
#include "debug/debug.c"
#include "alloc/alloc.c"
#include "gc/gc.c"
#include "img/img.c"
#include "mem/mem.c"
