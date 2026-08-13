/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/alloc.h"

#include <stdlib.h>

/* Zero-initialized: every member NULL, matching this module's own
 * "default passthrough to libc" contract without needing an explicit
 * constructor. */
static rcp_alloc_hooks_t g_hooks;

//cfusa:req REQ-ALLOC-001
void rcp_alloc_set_hooks(const rcp_alloc_hooks_t *hooks)
{
    if (!hooks) {
        rcp_alloc_reset_hooks();
        return;
    }
    g_hooks = *hooks;
}

//cfusa:req REQ-ALLOC-002
void rcp_alloc_reset_hooks(void)
{
    g_hooks.malloc_fn  = NULL;
    g_hooks.calloc_fn  = NULL;
    g_hooks.realloc_fn = NULL;
    g_hooks.free_fn    = NULL;
}

//cfusa:req REQ-ALLOC-003
void *rcp_malloc(size_t size)
{
    if (g_hooks.malloc_fn) return g_hooks.malloc_fn(size);
    return malloc(size);
}

//cfusa:req REQ-ALLOC-004
void *rcp_calloc(size_t nmemb, size_t size)
{
    if (g_hooks.calloc_fn) return g_hooks.calloc_fn(nmemb, size);
    return calloc(nmemb, size);
}

//cfusa:req REQ-ALLOC-006
void *rcp_realloc(void *ptr, size_t size)
{
    if (g_hooks.realloc_fn) return g_hooks.realloc_fn(ptr, size);
    return realloc(ptr, size);
}

//cfusa:req REQ-ALLOC-005
void rcp_free(void *ptr)
{
    if (g_hooks.free_fn) {
        g_hooks.free_fn(ptr);
        return;
    }
    free(ptr);
}
