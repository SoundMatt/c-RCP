/*
 * Pure-C UDP transport for the RCP protocol.
 *
 * On POSIX (Linux, macOS): full implementation using BSD sockets.
 * On Windows: stub that returns RCP_ERR_CLOSED from every operation (no
 * BSD-sockets implementation there yet — see ROADMAP.md).
 *
 * Frame format is defined in rcp/wire.h.
 */
#ifndef RCP_UDP_H
#define RCP_UDP_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handler invoked by an rcp_udp_zone_server_t when it receives a Command.
 * If NULL, the server replies RCP_RESPONSE_OK with an empty payload.
 * *out is zeroed before the handler runs; ownership rules match
 * rcp_mock_handler_fn (see mock.h): if the handler wants a response
 * payload, it must allocate it itself (e.g. via rcp_bytes_dup). */
typedef void (*rcp_udp_handler_fn)(const rcp_command_t *cmd, rcp_response_t *out, void *user_data);

/* ── ZoneServer: listens on a UDP port, dispatches Commands, publishes Status ── */

typedef struct rcp_udp_zone_server rcp_udp_zone_server_t;

/* Binds to addr:port (addr NULL or "" binds INADDR_ANY; port 0 lets the OS
 * assign an ephemeral port — see rcp_udp_zone_server_port()). Returns NULL
 * only on allocation failure; check rcp_udp_zone_server_ok() to distinguish
 * a successful bind from a socket/bind failure (mirrors cpp-RCP's ok()). */
rcp_udp_zone_server_t *rcp_udp_zone_server_new(rcp_zone_t zone, const char *addr, uint16_t port);

bool rcp_udp_zone_server_ok(const rcp_udp_zone_server_t *srv);

/* Writes "host:port\0" into buf (up to buf_len bytes) and returns the
 * string length excluding the NUL; returns 0 (buf untouched) on failure. */
size_t rcp_udp_zone_server_addr_string(const rcp_udp_zone_server_t *srv, char *buf, size_t buf_len);

uint16_t rcp_udp_zone_server_port(const rcp_udp_zone_server_t *srv);

void rcp_udp_zone_server_set_handler(rcp_udp_zone_server_t *srv, rcp_udp_handler_fn handler, void *user_data);
void rcp_udp_zone_server_set_healthy(rcp_udp_zone_server_t *srv, bool healthy);

/* Publishes a Status update to every subscriber that has sent a Subscribe
 * control frame. payload may be NULL iff len==0. */
void rcp_udp_zone_server_publish(rcp_udp_zone_server_t *srv, const uint8_t *payload, size_t len);

/* Stops the server's receive loop and closes the socket. Safe to call more
 * than once; blocks until the internal serve thread has fully exited. */
void rcp_udp_zone_server_close(rcp_udp_zone_server_t *srv);

/* Frees the server. Call after rcp_udp_zone_server_close() (close() is
 * idempotent and safe to call again here if not already closed). */
void rcp_udp_zone_server_destroy(rcp_udp_zone_server_t *srv);

/* ── Controller: dials a ZoneServer, implements rcp_controller_t ─────────────── */

/* Connects to server_host:server_port for zone. Returned with refcount 1
 * (release with rcp_controller_release()) even on connect failure — check
 * rcp_udp_controller_ok() first; a not-ok controller's send()/subscribe()
 * always return RCP_ERR_CLOSED. */
rcp_controller_t *rcp_udp_controller_new(rcp_zone_t zone, const char *server_host, uint16_t server_port);

bool rcp_udp_controller_ok(rcp_controller_t *ctrl);

/* ── Registry: rcp_registry_t backed by UDP controllers ──────────────────────── */

rcp_registry_t *rcp_udp_registry_new(void);

/* Dials server_host:server_port for zone and registers the resulting
 * controller (taking a reference; the registry owns the dialed controller
 * going forward). Returns RCP_ERR_NOT_FOUND if the dial itself fails
 * (mirrors cpp-RCP's udp::Registry::dial), or the usual register_ctrl()
 * outcomes (RCP_ERR_ALREADY_EXISTS / RCP_ERR_CLOSED / RCP_OK) otherwise. */
int rcp_udp_registry_dial(rcp_registry_t *reg, rcp_zone_t zone, const char *server_host, uint16_t server_port);

#ifdef __cplusplus
}
#endif

#endif /* RCP_UDP_H */
