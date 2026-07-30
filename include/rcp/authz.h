/* SPDX-License-Identifier: MPL-2.0 */
/*
 * authz.h -- Request-level access control (ISO 21434 / IEC 62443 SL-2) for
 * the TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 21,
 * "Satellite Package Rework", milestone 80, "Generic decorators, batch 1").
 *
 * This is this module's own ADAPT-class rebind, not a from-scratch
 * REPLACE: the "check the caller's identity against a policy table before
 * a request goes out" shape survives unchanged (per the Satellite
 * Disposition table's own "access-control-before-send stays
 * protocol-agnostic in shape" reasoning) -- only what a policy entry is
 * keyed on changes, from the retired rcp_zone_t/rcp_command_type_t pair to
 * avtp.h's rcp_avtp_addr_t (stream_id + byte_bus_id) plus a caller-supplied
 * request_type byte.
 *
 * request_type is deliberately left an opaque uint8_t this module never
 * itself interprets, matching the layering discipline every request-kind
 * module in this codebase already follows (see e.g. e2e.h's file header):
 * for a Standard request it is natural to pass acf.h's rcp_acf_op_t (cast
 * to uint8_t); for a compound/chained/triggered/timed/cancel request it is
 * natural to pass that module's own request_type opcode (request_compound.h
 * and siblings). This module treats whichever byte the caller passes as
 * nothing more than a policy-matching label -- it never reaches into
 * acf.h/avtp.h itself to derive one, the same "operate on caller-owned,
 * already-classified data" convention regmap.h's rcp_regmap_writer_ctx()
 * and every ep_*.h codec already use.
 *
 * There is no longer a single generic rcp_controller_t::send() choke point
 * to interpose a wrapper on (ROADMAP.md's Protocol Replacement Notice --
 * Phase 16/19 built 13 heterogeneous, independently-typed endpoint
 * modules, each with its own encode/apply function pairs). Rather than
 * inventing a new generic wrapper type with no real single call to wrap,
 * this module drops the old AuthController vtable-wrapper entirely:
 * rcp_authz_policy_permit() is now the whole interception point, called
 * directly by the caller immediately before it drives whichever
 * endpoint-specific encode/send call applies -- the same caller-driven,
 * "sends no wire traffic and owns no transport itself" shape milestone
 * 79's watchdog.c/deadline.c/powerstate.c already established for this
 * kind of decorator.
 *
 * The caller identity is still a short string label (certificate CN or
 * pre-shared key label); full certificate-chain validation remains the
 * responsibility of whatever link-layer security control is in effect
 * (MACsec / 802.1AE, per ROADMAP.md's Satellite Disposition table -- see
 * rcp/tls.h's own removal note, v0.78.0) -- this module only ever checks
 * the presented identity against the policy table.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_AUTHZ_H
#define RCP_AUTHZ_H

#include "rcp/avtp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length (including the NUL terminator) of an identity string
 * passed to rcp_authz_policy_allow() or rcp_authz_policy_permit().
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

/* Adds a permission entry: identity may issue a request of any type in
 * request_types (or of any type at all, if n_request_types == 0) to any
 * address in addrs (or any address at all, if n_addrs == 0). identity is
 * copied (truncated to RCP_AUTHZ_IDENTITY_MAX-1 bytes); addrs/request_types
 * are copied by value into storage this policy owns. Thread-safe with
 * concurrent rcp_authz_policy_permit() calls. Returns false on allocation
 * failure (entry not added). */
bool rcp_authz_policy_allow(rcp_authz_policy_t *policy, const char *identity,
                             const rcp_avtp_addr_t *addrs, size_t n_addrs,
                             const uint8_t *request_types, size_t n_request_types);

/* Returns whether identity is permitted to issue a request_type request to
 * addr, per every entry added so far (an identity with no matching entry
 * at all is permitted nothing -- the default-deny policy this module has
 * always used). Thread-safe. */
bool rcp_authz_policy_permit(rcp_authz_policy_t *policy, const char *identity,
                              rcp_avtp_addr_t addr, uint8_t request_type);

#ifdef __cplusplus
}
#endif

#endif /* RCP_AUTHZ_H */
