/*
 * Per-zone token-bucket admission control against command flooding
 * (SG-009, H-009).
 *
 * Wraps any rcp_controller_t and enforces a sustained rate limit and burst
 * capacity. RCP_PRIORITY_CRITICAL commands bypass the bucket by default so
 * watchdog kicks and emergency actuations are never throttled. All other
 * commands consume one token; send() returns RCP_ERR_BUSY immediately when
 * the bucket is exhausted.
 */
#ifndef RCP_RATELIMIT_H
#define RCP_RATELIMIT_H

#include "rcp/rcp.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double rate;            /* sustained token refill rate, tokens/second (default: 100.0) */
    int    burst;           /* maximum token accumulation (default: 20) */
    bool   exempt_critical; /* if true, RCP_PRIORITY_CRITICAL bypasses the bucket (default: true) */
} rcp_ratelimit_config_t;

/* ASIL-B recommended values: { rate = 100.0, burst = 20, exempt_critical = true }. */
rcp_ratelimit_config_t rcp_ratelimit_default_config(void);

/* Wraps inner (retains it) and applies token-bucket admission control to
 * every send(). Returned with refcount 1; release with
 * rcp_controller_release(), which also releases this wrapper's reference
 * to inner. */
rcp_controller_t *rcp_ratelimit_controller_new(rcp_controller_t *inner, rcp_ratelimit_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RATELIMIT_H */
