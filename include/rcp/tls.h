/*
 * Mutual TLS transport for zone controller communication (SG-006, IEC 62443
 * SL-2).
 *
 * This is a compile-time interface stub: no OpenSSL (or equivalent) backend
 * is linked, so every transport call returns RCP_ERR_NOT_SUPPORTED rather
 * than transmitting an unencrypted frame — the safety contract that matters
 * is that this stub never silently falls back to plaintext. Wiring in a
 * real OpenSSL backend is future work (see ROADMAP.md); mirrors cpp-RCP's
 * own tls.hpp, which ships the same stub absent RCP_TLS_OPENSSL.
 */
#ifndef RCP_TLS_H
#define RCP_TLS_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *cert_file;   /* PEM certificate for this endpoint */
    const char *key_file;    /* PEM private key for this endpoint */
    const char *ca_file;     /* PEM CA bundle for peer verification */
    bool        verify_peer; /* enforce mutual authentication */
} rcp_tls_config_t;

/* Returns a config with verify_peer=true (mutual auth enforced unless
 * explicitly disabled) and all file paths NULL. */
rcp_tls_config_t rcp_tls_config_default(void);

/* Handler invoked by an rcp_tls_zone_server_t — same shape as
 * rcp_udp_handler_fn, kept as its own type per the existing per-transport
 * handler convention (see mock.h/udp.h). Never actually invoked while this
 * module has no backend. */
typedef void (*rcp_tls_handler_fn)(const rcp_command_t *cmd, rcp_response_t *out, void *user_data);

/* Connects to server_host:server_port for zone over mTLS. Every operation on
 * the returned controller currently returns RCP_ERR_NOT_SUPPORTED (no
 * backend). Returned with refcount 1; release with rcp_controller_release(). */
rcp_controller_t *rcp_tls_controller_new(rcp_zone_t zone, const char *server_host,
                                          uint16_t server_port, rcp_tls_config_t config);

/* ── ZoneServer (interface stub) ──────────────────────────────────────────── */

typedef struct rcp_tls_zone_server rcp_tls_zone_server_t;

rcp_tls_zone_server_t *rcp_tls_zone_server_new(rcp_zone_t zone, const char *addr,
                                                uint16_t port, rcp_tls_config_t config);
/* Always false: this stub never has a working backend. */
bool rcp_tls_zone_server_ok(const rcp_tls_zone_server_t *srv);
void rcp_tls_zone_server_set_handler(rcp_tls_zone_server_t *srv, rcp_tls_handler_fn handler, void *user_data);
void rcp_tls_zone_server_set_healthy(rcp_tls_zone_server_t *srv, bool healthy);
void rcp_tls_zone_server_publish(rcp_tls_zone_server_t *srv, const uint8_t *payload, size_t len);
void rcp_tls_zone_server_close(rcp_tls_zone_server_t *srv);
void rcp_tls_zone_server_destroy(rcp_tls_zone_server_t *srv);

/* ── Registry ──────────────────────────────────────────────────────────────── */

rcp_registry_t *rcp_tls_registry_new(void);

/* Always returns RCP_ERR_NOT_SUPPORTED (no backend to dial with). */
int rcp_tls_registry_dial(rcp_registry_t *reg, rcp_zone_t zone, const char *server_host,
                           uint16_t server_port, rcp_tls_config_t config);

#ifdef __cplusplus
}
#endif

#endif /* RCP_TLS_H */
