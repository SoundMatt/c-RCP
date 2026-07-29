/*
 * Command-level access control (ISO 21434 / IEC 62443 SL-2).
 *
 * rcp_authz_controller_new() wraps any rcp_controller_t and checks each
 * Command against a signed rcp_authz_policy_t before forwarding. A policy
 * declares which caller identities may send which command types to which
 * zones.
 *
 * The caller identity is a short string label (certificate CN or
 * pre-shared key label). Full certificate-chain validation is the
 * responsibility of whatever link-layer security control is in effect
 * (MACsec / 802.1AE, per ROADMAP.md's Satellite Disposition table --
 * rcp/tls.h was removed at v0.78.0, see that table's own reasoning);
 * authz only checks the presented identity against the policy table.
 */
#ifndef RCP_AUTHZ_H
#define RCP_AUTHZ_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length (including the NUL terminator) of an identity string
 * passed to rcp_authz_policy_allow() or rcp_authz_controller_set_identity().
 * Identities are short labels (certificate CN or PSK label) in practice;
 * longer strings are truncated. */
#define RCP_AUTHZ_IDENTITY_MAX 128

typedef struct rcp_authz_policy rcp_authz_policy_t;

/* Creates an empty access policy (denies everyone until entries are added
 * via rcp_authz_policy_allow()). Returned with refcount 1; release with
 * rcp_authz_policy_release(). Returns NULL on allocation failure. */
rcp_authz_policy_t *rcp_authz_policy_new(void);

rcp_authz_policy_t *rcp_authz_policy_retain(rcp_authz_policy_t *p);
void                 rcp_authz_policy_release(rcp_authz_policy_t *p);

/* Adds a permission entry: identity may send any command type in
 * cmd_types (or any type at all, if n_cmd_types == 0) to any zone in zones
 * (or any zone at all, if n_zones == 0). identity is copied (truncated to
 * RCP_AUTHZ_IDENTITY_MAX-1 bytes); zones/cmd_types are copied by value.
 * Thread-safe with concurrent rcp_authz_policy_permit() calls. Returns
 * false on allocation failure (entry not added). */
bool rcp_authz_policy_allow(rcp_authz_policy_t *policy, const char *identity,
                             const rcp_zone_t *zones, size_t n_zones,
                             const rcp_command_type_t *cmd_types, size_t n_cmd_types);

/* Returns whether identity is permitted to send a command of the given
 * type to the given zone, per every entry added so far. Thread-safe. */
bool rcp_authz_policy_permit(rcp_authz_policy_t *policy, const char *identity,
                              rcp_zone_t zone, rcp_command_type_t type);

/* Resolves the current caller identity for a send() call (e.g. from a
 * thread-local or TLS session attribute). Returns a string valid for the
 * duration of the call (the authz controller does not retain it beyond
 * that). user_data is the opaque pointer passed to
 * rcp_authz_controller_new(). */
typedef const char *(*rcp_authz_identity_fn)(void *user_data);

/* Wraps inner (retains it) and policy (retains it) and enforces policy on
 * every send(). If identity_fn is non-NULL, it is called on every send()
 * to resolve the caller identity, taking precedence over any identity set
 * via rcp_authz_controller_set_identity(). Returned with refcount 1;
 * release with rcp_controller_release(), which also releases this
 * wrapper's references to inner and policy. */
rcp_controller_t *rcp_authz_controller_new(rcp_controller_t *inner, rcp_authz_policy_t *policy,
                                            rcp_authz_identity_fn identity_fn, void *identity_fn_user_data);

/* Sets a fixed caller identity for this controller instance, used when no
 * identity_fn was provided (or is ignored if one was). identity is copied
 * (truncated to RCP_AUTHZ_IDENTITY_MAX-1 bytes). ctrl must have been
 * created by rcp_authz_controller_new(). */
void rcp_authz_controller_set_identity(rcp_controller_t *ctrl, const char *identity);

#ifdef __cplusplus
}
#endif

#endif /* RCP_AUTHZ_H */
