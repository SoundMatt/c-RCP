/*
 * Atomic multi-zone command broadcast with typed zone group sets.
 *
 * rcp_zone_group_t is a value type enumerating a subset of zones.
 * rcp_zonegroup_send() dispatches one command to every zone in the group
 * concurrently and collects per-zone responses into a rcp_group_response_t.
 */
#ifndef RCP_ZONEGROUP_H
#define RCP_ZONEGROUP_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum zones a single rcp_zone_group_t can hold. Generous headroom over
 * the protocol's 5 real zones; a fixed capacity (rather than cpp-RCP's
 * unbounded std::vector) is what makes plain struct assignment a full,
 * independent copy -- no heap buffer to alias or realloc underneath a
 * caller who still holds the original. */
#define RCP_ZONE_GROUP_MAX 8

typedef struct {
    rcp_zone_t zones[RCP_ZONE_GROUP_MAX];
    size_t     len;
} rcp_zone_group_t;

/* An empty group ({ .len = 0 }). */
rcp_zone_group_t rcp_zone_group_empty(void);

/* front-left, front-right, rear-left, rear-right, central (5 zones). */
rcp_zone_group_t rcp_zone_group_all(void);

/* rear-left, rear-right (2 zones). */
rcp_zone_group_t rcp_zone_group_rear(void);

/* front-left, front-right (2 zones). */
rcp_zone_group_t rcp_zone_group_front(void);

/* Appends z to g. Returns false without modifying g if g is already at
 * RCP_ZONE_GROUP_MAX capacity. */
bool rcp_zone_group_add(rcp_zone_group_t *g, rcp_zone_t z);

typedef struct {
    rcp_zone_t     zone;
    rcp_response_t response; /* owned; free via rcp_group_response_free() */
    int             error;    /* an rcp_errc_t; RCP_OK if this zone's send succeeded */
} rcp_zone_result_t;

typedef struct {
    rcp_zone_result_t *results;     /* owned; one entry per zone in the dispatched group */
    size_t              results_len;
} rcp_group_response_t;

/* Returns true iff every result succeeded (error == RCP_OK and
 * response.status == RCP_RESPONSE_OK). */
bool rcp_group_response_ok(const rcp_group_response_t *r);

/* Fills out[0..min(count,cap)) with the zones whose result failed, and
 * returns the total count of failing zones (which may exceed cap; callers
 * needing all of them should re-call with a larger buffer sized to the
 * returned count), matching rcp_registry_controllers()'s convention. */
size_t rcp_group_response_errors(const rcp_group_response_t *r, rcp_zone_t *out, size_t cap);

/* Frees every result's response payload and the results array itself, then
 * zeroes *r. Safe to call on an already-freed or zero-initialized
 * rcp_group_response_t. */
void rcp_group_response_free(rcp_group_response_t *r);

/* Dispatches cmd to every zone in group concurrently (one background
 * thread per zone) via reg, and collects per-zone results. ctx is
 * propagated unchanged to every per-zone send. A zone not currently
 * registered in reg produces a result with the lookup's error code and a
 * zeroed response, rather than aborting the whole broadcast. The returned
 * rcp_group_response_t must eventually be released with
 * rcp_group_response_free(). Returns a zeroed (results = NULL, results_len
 * = 0) response if group is empty or on allocation failure. */
rcp_group_response_t rcp_zonegroup_send(rcp_registry_t *reg, const rcp_context_t *ctx,
                                         const rcp_zone_group_t *group, const rcp_command_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ZONEGROUP_H */
