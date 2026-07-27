/*
 * Zero-copy intra-host command delivery via shared in-process memory.
 *
 * rcp_shmem_zone_server_t and its paired rcp_controller_t communicate via
 * direct in-process calls (no serialization) — this is the default "shared
 * memory" implementation for unit testing and single-process deployments,
 * ported from cpp-RCP's shmem.hpp. True cross-process OS shared memory
 * (shm_open/mmap) is future work (cpp-RCP scopes it as RCP_SHMEM_POSIX,
 * still unimplemented there too).
 */
#ifndef RCP_SHMEM_H
#define RCP_SHMEM_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rcp_shmem_handler_fn)(const rcp_command_t *cmd, rcp_response_t *out, void *user_data);

/* ── ZoneServer — zone-controller side of the in-process transport ─────────── */

typedef struct rcp_shmem_zone_server rcp_shmem_zone_server_t;

/* Created with refcount 1 and healthy=true (matches cpp-RCP's default).
 * Refcounted because one or more rcp_controller_t instances (and their
 * subscribe watcher threads) hold their own reference to their paired
 * server independent of the caller's. */
rcp_shmem_zone_server_t *rcp_shmem_zone_server_new(rcp_zone_t zone);
rcp_shmem_zone_server_t *rcp_shmem_zone_server_retain(rcp_shmem_zone_server_t *srv);
void                     rcp_shmem_zone_server_release(rcp_shmem_zone_server_t *srv);

rcp_zone_t rcp_shmem_zone_server_zone(const rcp_shmem_zone_server_t *srv);
void rcp_shmem_zone_server_set_handler(rcp_shmem_zone_server_t *srv, rcp_shmem_handler_fn handler, void *user_data);
void rcp_shmem_zone_server_set_healthy(rcp_shmem_zone_server_t *srv, bool healthy);

/* Publishes a Status update to every subscriber added via add_sub(). */
void rcp_shmem_zone_server_publish(rcp_shmem_zone_server_t *srv, const uint8_t *payload, size_t len);

/* Processes one command through the configured handler (or the default
 * OK-with-empty-payload response if none is set). Returns false if the
 * server is closed (out left untouched in that case). Called by
 * rcp_shmem_controller_t's send(); exposed publicly for API parity with
 * cpp-RCP's shmem::ZoneServer::dispatch_one(). */
bool rcp_shmem_zone_server_dispatch_one(rcp_shmem_zone_server_t *srv, const rcp_command_t *cmd, rcp_response_t *out);

/* Registers/deregisters a subscriber channel. Exposed publicly for API
 * parity with cpp-RCP; used internally by rcp_shmem_controller_t's
 * subscribe(). Does not retain/release ch — the caller manages that. */
void rcp_shmem_zone_server_add_sub(rcp_shmem_zone_server_t *srv, rcp_status_channel_t *ch);
void rcp_shmem_zone_server_remove_sub(rcp_shmem_zone_server_t *srv, rcp_status_channel_t *ch);

void rcp_shmem_zone_server_close(rcp_shmem_zone_server_t *srv);
bool rcp_shmem_zone_server_ok(const rcp_shmem_zone_server_t *srv);

/* ── Controller — wraps a ZoneServer, implements rcp_controller_t ───────────── */

/* Takes its own reference to server (retains it) — release your own
 * reference to server separately if you still need it after this call.
 * Returned with refcount 1; release with rcp_controller_release(). */
rcp_controller_t *rcp_shmem_controller_new(rcp_shmem_zone_server_t *server);

/* ── Registry ──────────────────────────────────────────────────────────────── */

rcp_registry_t *rcp_shmem_registry_new(void);

/* Wraps server in a new rcp_shmem_controller_t (see rcp_shmem_controller_new)
 * and registers it. */
int rcp_shmem_registry_add_server(rcp_registry_t *reg, rcp_shmem_zone_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SHMEM_H */
