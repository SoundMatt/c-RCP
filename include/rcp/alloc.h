/* SPDX-License-Identifier: MPL-2.0 */
/*
 * alloc.h -- a pluggable, global dynamic-memory indirection for this
 * library's own internal heap allocations (ROADMAP.md, issue #338,
 * REQ-SEQ-002's own fault-injection need).
 *
 * ── Why this exists ──────────────────────────────────────────────────────────
 *
 * Two independent motivations, not one:
 *
 *   1. An integrator embedding this library into a safety-relevant system
 *      routinely needs to route, bound, or monitor every heap allocation
 *      a third-party component performs -- a fixed memory pool, a
 *      allocation-tracking wrapper for a certification audit trail, or a
 *      hard cap enforced independently of the OS allocator. Until now,
 *      this library called libc's malloc()/calloc()/free() directly from
 *      inside individual .c files (request_sequencer.c,
 *      fragment.c, and others), giving an integrator no seam to intercept
 *      that at all short of a link-time libc override -- itself
 *      non-portable across this project's own three target OSes
 *      (Linux/macOS/Windows).
 *   2. This library's own test suite has no portable way to prove an
 *      allocation-failure branch is handled correctly (REQ-SEQ-002:
 *      rcp_sequencer_table_new() returning a zeroed table when malloc()
 *      fails) without either a real, practically-unreachable OOM
 *      condition, or a non-portable technique (glibc malloc-symbol
 *      interposition works on Linux only; macOS's two-level namespace
 *      blocks the equivalent trick without a DYLD_INTERPOSE macro; MSVC
 *      has its own, again different, mechanism). A plain C function-
 *      pointer indirection sidesteps all three platforms' own linker
 *      quirks by construction -- it is just an ordinary function call.
 *      An absurdly large size (e.g. requesting near SIZE_MAX octets) is
 *      NOT such a portable technique either, despite forcing a genuine
 *      libc realloc() failure on a plain debug build: under this
 *      project's own CI AddressSanitizer configuration
 *      (ASAN_OPTIONS=halt_on_error=1:abort_on_error=1, no
 *      allocator_may_return_null override), ASan treats any request
 *      over its own internal max-supported-size ceiling as a hard abort,
 *      not a NULL return -- REQ-FRAG-016's own test hit exactly this and
 *      is why rcp_realloc() exists below rather than reusing the
 *      REQ-SEQ-002-era "make libc fail for real" trick a second time.
 *
 * ── Scope: additive, not a sweeping rewrite ─────────────────────────────────
 *
 * This module does NOT retrofit every malloc()/calloc()/free() call site
 * in this codebase -- that would be a large, wide-blast-radius change
 * (roughly as invasive as REQ-RMAP-055's own W-plus primitive
 * deliberately avoided becoming, by the identical reasoning: a real
 * behavior addition should not force every existing, unrelated call site
 * through a mechanical edit it doesn't need). A module opts in by
 * calling rcp_malloc()/rcp_calloc()/rcp_free() instead of the libc
 * functions directly, same signatures, same failure convention (NULL on
 * failure, exactly like libc's own). request_sequencer.c is the first
 * caller (REQ-SEQ-002); other modules may opt in later, individually, as
 * their own fault-injection needs arise -- this header makes that
 * possible without inventing a second seam each time.
 *
 * ── Default behavior: an invisible passthrough ──────────────────────────────
 *
 * With no hooks installed (rcp_alloc_reset_hooks(), also this module's
 * own zero-init default), rcp_malloc()/rcp_calloc()/rcp_free() are a
 * transparent passthrough to malloc()/calloc()/free() -- a caller that
 * never touches this module's own hook-installing functions observes
 * identical behavior to calling libc directly, at the cost of one extra
 * function-pointer indirection per call. No global state is touched
 * unless rcp_alloc_set_hooks() is called at least once.
 *
 * ── Not thread-safe by design ────────────────────────────────────────────────
 *
 * The hook table is a single, unsynchronized global -- installing hooks
 * from one thread while another thread is mid-allocation is undefined,
 * the same caveat every other global-mutable-config primitive in this
 * codebase already carries (e.g. rcp_regmap_general_t itself has no
 * internal locking of its own). This module's own intended use --
 * installing hooks once, at test or integration setup, before any
 * allocation this library performs -- never needs concurrent
 * installation, so this is a deliberate simplicity choice, not an
 * oversight.
 */
#ifndef RCP_ALLOC_H
#define RCP_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*rcp_alloc_malloc_fn)(size_t size);
typedef void *(*rcp_alloc_calloc_fn)(size_t nmemb, size_t size);
typedef void *(*rcp_alloc_realloc_fn)(void *ptr, size_t size);
typedef void  (*rcp_alloc_free_fn)(void *ptr);

/* A caller-owned set of hooks to install. Any member left NULL falls
 * back to the corresponding libc function for that one operation --
 * a caller wanting to intercept malloc() alone, leaving calloc()/free()
 * untouched, may leave those two members NULL rather than having to
 * re-implement libc's own behavior for them. */
typedef struct {
    rcp_alloc_malloc_fn  malloc_fn;
    rcp_alloc_calloc_fn  calloc_fn;
    rcp_alloc_realloc_fn realloc_fn;
    rcp_alloc_free_fn    free_fn;
} rcp_alloc_hooks_t;

/* Installs hooks globally, replacing whatever was installed before (if
 * anything). hooks is copied by value; the caller does not need to keep
 * it alive past this call. Passing a hooks value with every member NULL
 * is equivalent to rcp_alloc_reset_hooks(). */
void rcp_alloc_set_hooks(const rcp_alloc_hooks_t *hooks);

/* Restores the default passthrough-to-libc behavior, discarding whatever
 * hooks were previously installed. Safe to call even if no hooks were
 * ever installed (a no-op in that case). */
void rcp_alloc_reset_hooks(void);

/* rcp_malloc(size)/rcp_calloc(nmemb, size)/rcp_realloc(ptr, size)/
 * rcp_free(ptr): this module's own indirected counterparts of
 * malloc()/calloc()/realloc()/free(), same signatures, same failure
 * convention (NULL on failure, ptr left untouched exactly like libc's
 * own realloc(); rcp_free(NULL) is a safe no-op exactly like free(NULL)
 * already is). A module opting into this indirection calls these
 * instead of the libc functions directly -- everything else about how
 * it handles a NULL return is unchanged. */
void *rcp_malloc(size_t size);
void *rcp_calloc(size_t nmemb, size_t size);
void *rcp_realloc(void *ptr, size_t size);
void  rcp_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ALLOC_H */
