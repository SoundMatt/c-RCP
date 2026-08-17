/* SPDX-License-Identifier: MPL-2.0 */
/* Internal, not installed: a single allocation-size overflow guard shared
 * by every growable-array/allocation call site this library's own
 * malloc()/realloc() sizing multiplies an element count by an element
 * size (CFUSA-CY005, CERT-C INT30-C, CWE-190; issue #523).
 *
 * Every call site this guards was individually reviewed as part of
 * issue #523 and found provably safe today -- the multiplicand is either
 * an internal geometric-doubling capacity counter (unreachable overflow
 * range: the process would need on the order of SIZE_MAX / (2 *
 * sizeof(element)) live entries already held, long past memory
 * exhaustion) or a value pre-capped by rcp_fragment_plan_count() at
 * RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS. This header is defense-in-depth
 * against a future refactor accidentally removing one of those bounds,
 * not a fix for a live defect -- see issue #523 for the full per-site
 * analysis. */
#ifndef RCP_INTERNAL_ALLOC_OVERFLOW_H
#define RCP_INTERNAL_ALLOC_OVERFLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True iff n * elem_size would overflow size_t. elem_size must be > 0 --
 * every call site here passes sizeof(...), which the language already
 * guarantees is nonzero for any complete object type. */
static inline bool rcp_alloc_size_would_overflow(size_t n, size_t elem_size)
{
    return n > (SIZE_MAX / elem_size);
}

/* n * elem_size, or 0 if that would overflow size_t. Every call site in
 * this codebase that uses this already has n > 0 (a zero-length
 * allocation is never requested through this helper), so a 0 return is
 * an unambiguous "would overflow" signal the caller can branch on
 * without a separate bool out-parameter. */
static inline size_t rcp_alloc_checked_size(size_t n, size_t elem_size)
{
    return rcp_alloc_size_would_overflow(n, elem_size) ? 0 : n * elem_size;
}

#endif /* RCP_INTERNAL_ALLOC_OVERFLOW_H */
