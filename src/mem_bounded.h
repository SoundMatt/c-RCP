/* SPDX-License-Identifier: MPL-2.0 */
/* Internal, not installed: explicit-size-bounded copy primitives shared by
 * every memcpy()/memmove()/strncpy() call site in this library (CFUSA-CY001,
 * CERT-C STR31-C, CWE-120; issue #523).
 *
 * Every call site this replaces was individually reviewed as part of
 * issue #523 and found provably safe today: destination is either
 * allocated to exactly the copied length, guarded by an explicit
 * length-vs-capacity check immediately before the copy, or a fixed-size
 * copy between equal-size fields (e.g. a 6-byte MAC address). n <= dst_cap
 * already holds at every call site before this header existed -- these
 * wrappers make that invariant explicit and machine-checkable at the call
 * site rather than changing behavior. This is defense-in-depth against a
 * future refactor accidentally removing one of those existing guards, not
 * a fix for a live defect -- see issue #523 for the full per-site analysis.
 *
 * n > dst_cap is treated as "should never happen" (every caller already
 * guarantees it does not): the copy is skipped rather than performed, so a
 * future regression that violates the invariant fails safe (destination
 * left unchanged) instead of overrunning the destination buffer.
 */
#ifndef RCP_INTERNAL_MEM_BOUNDED_H
#define RCP_INTERNAL_MEM_BOUNDED_H

#include <stddef.h>
#include <string.h>

/* Copies n bytes from src to dst iff n <= dst_cap; a no-op otherwise. */
static inline void rcp_memcpy_bounded(void *dst, size_t dst_cap,
                                       const void *src, size_t n)
{
    if (n <= dst_cap) {
        memcpy(dst, src, n);
    }
}

/* Same as rcp_memcpy_bounded() but for possibly-overlapping regions. */
static inline void rcp_memmove_bounded(void *dst, size_t dst_cap,
                                        const void *src, size_t n)
{
    if (n <= dst_cap) {
        memmove(dst, src, n);
    }
}

/* Hand-rolled equivalent of POSIX strnlen(), used instead of the real
 * one below because strnlen() is a POSIX.1-2008/XSI extension, not
 * ISO C99: under this project's own strict `-std=c99`
 * (CMAKE_C_EXTENSIONS OFF, no _POSIX_C_SOURCE/_DEFAULT_SOURCE feature-test
 * macro defined anywhere), glibc's <string.h> does not declare it. Every
 * pre-c-RCP-16 compiler this project's CI used (gcc-12, clang-14) only
 * *warned* on the resulting implicit-function-declaration and happened to
 * link successfully against glibc's still-present symbol; clang 15+
 * (introduced to CI by the c-RCP-16 `mcdc` job's clang-18 requirement,
 * needed for -fcoverage-mcdc) treats an implicit declaration as a hard
 * error by default, surfacing this latent non-conformance for the first
 * time. Fixed at the root instead of reaching for a feature-test-macro
 * workaround, consistent with this header's own "explicit bounded
 * primitives, not a libc dependency" ethos. */
static inline size_t rcp_strnlen_bounded(const char *s, size_t maxlen)
{
    size_t i = 0;
    while (i < maxlen && s[i] != '\0') {
        i++;
    }
    return i;
}

/* Copies up to dst_cap-1 bytes of src into dst and always NUL-terminates
 * within dst_cap bytes (unlike bare strncpy(), which does not terminate
 * if src's length is >= dst_cap, and every call site this replaces
 * requested exactly n == dst_cap-1). dst_cap must be the true capacity of
 * dst in bytes, including room for the terminator; dst_cap == 0 is a
 * no-op (there is no room even for a terminator). */
static inline void rcp_strncpy_bounded(char *dst, size_t dst_cap,
                                        const char *src)
{
    if (dst_cap == 0) {
        return;
    }
    size_t len = rcp_strnlen_bounded(src, dst_cap - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#endif /* RCP_INTERNAL_MEM_BOUNDED_H */
