/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/alloc.h"

#include <stdlib.h>

/* Zero-initialized: every member NULL, matching this module's own
 * "default passthrough to libc" contract without needing an explicit
 * constructor. */
static rcp_alloc_hooks_t g_hooks;

/* [c-RCP-23b], issue #600: zero-initialized (unlocked) by default, same
 * "no explicit constructor needed" reasoning as g_hooks above. See
 * alloc.h's own "Locking the hook table" doc section for the design. */
static bool g_locked;

//cfusa:req REQ-ALLOC-001
//cfusa:req REQ-ALLOC-010
bool rcp_alloc_set_hooks(const rcp_alloc_hooks_t *hooks)
{
    if (g_locked) return false;
    if (!hooks) {
        return rcp_alloc_reset_hooks();
    }
    g_hooks = *hooks;
    return true;
}

//cfusa:req REQ-ALLOC-002
//cfusa:req REQ-ALLOC-011
bool rcp_alloc_reset_hooks(void)
{
    if (g_locked) return false;
    g_hooks.malloc_fn  = NULL;
    g_hooks.calloc_fn  = NULL;
    g_hooks.realloc_fn = NULL;
    g_hooks.free_fn    = NULL;
    return true;
}

//cfusa:req REQ-ALLOC-007
bool rcp_alloc_lock_hooks(void)
{
    if (g_locked) return false;
    g_locked = true;
    return true;
}

//cfusa:req REQ-ALLOC-008
bool rcp_alloc_unlock_hooks(void)
{
    if (!g_locked) return false;
    g_locked = false;
    return true;
}

//cfusa:req REQ-ALLOC-009
bool rcp_alloc_hooks_locked(void)
{
    return g_locked;
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
    ptr = NULL;
}
