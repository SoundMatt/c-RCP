/* RELAY C bindings — relay_* types (RELAY spec §18.2, v1.10).
 *
 * Defines the protocol-agnostic subset of the relay spec that rcp.h depends
 * on: the mandatory error-condition sentinels (§5.1) and Context (§18.2).
 * This is a pure-C port of the same subset cpp-RCP's include/relay/relay.hpp
 * exposes to include/rcp/rcp.hpp; it is not a full RELAY binding.
 */
#ifndef RELAY_RELAY_H
#define RELAY_RELAY_H

#include <stdbool.h>
#include <stdint.h>

#include "rcp/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error conditions — mandatory sentinels (§5.1) ────────────────────────── */

typedef enum {
    RELAY_ERRC_CLOSED            = 0,
    RELAY_ERRC_NOT_CONNECTED     = 1,
    RELAY_ERRC_TIMEOUT           = 2,
    RELAY_ERRC_PAYLOAD_TOO_LARGE = 3,
} relay_errc_t;

const char *relay_strerror(relay_errc_t e);

/* ── Context (§18.2) ──────────────────────────────────────────────────────── */

typedef struct {
    bool     has_deadline;
    uint64_t deadline_ms; /* rcp_monotonic_ms() timestamp; valid only if has_deadline */
} relay_context_t;

static inline relay_context_t relay_context_background(void)
{
    relay_context_t ctx;
    ctx.has_deadline = false;
    ctx.deadline_ms  = 0;
    return ctx;
}

static inline relay_context_t relay_context_with_timeout_ms(uint64_t timeout_ms)
{
    relay_context_t ctx;
    ctx.has_deadline = true;
    ctx.deadline_ms  = rcp_monotonic_ms() + timeout_ms;
    return ctx;
}

static inline relay_context_t relay_context_with_deadline_ms(uint64_t deadline_ms)
{
    relay_context_t ctx;
    ctx.has_deadline = true;
    ctx.deadline_ms  = deadline_ms;
    return ctx;
}

static inline bool relay_context_done(const relay_context_t *ctx)
{
    if (!ctx->has_deadline) return false;
    return rcp_monotonic_ms() >= ctx->deadline_ms;
}

#ifdef __cplusplus
}
#endif

#endif /* RELAY_RELAY_H */
