/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Client-side power-mode convenience wrapper for the TC18 Remote Control
 * Protocol (SG-003, ISO 26262 ASIL-B) -- ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 79.
 *
 * This is this module's own full REPLACE of its pre-TC18 content: the old
 * rcp_powerstate_manager_t sent RCP_CMD_SLEEP/RCP_CMD_WAKE to zone
 * controllers and tracked an ad-hoc Active/Sleeping/BusOff state machine
 * of its own, retrying RCP_CMD_WAKE on a fixed interval whenever a send
 * failed. Neither RCP_CMD_SLEEP/RCP_CMD_WAKE, rcp_zone_t/rcp_controller_t,
 * nor the BusOff/recovery concept survive in the TC18 model this codebase
 * now targets (ROADMAP.md's Protocol Replacement Notice) -- there is no
 * generic command-failure-implies-bus-fault signal to recover from
 * anymore. This module's own Active/Sleeping/BusOff enum is dropped
 * outright per this milestone's own roadmap scope ("replacing the ad-hoc
 * Active/Sleeping/BusOff state machine entirely"), superseded by power.h's
 * rcp_pwrmode_t (Normal/StandBy/Sleep/Unpowered, milestone 75).
 *
 * This module is a thin client-side convenience over two already-shipped
 * protocol-core primitives, mirroring the "operate on caller-owned data,
 * own no transport" layering both of them already establish for
 * themselves (see power.h's and ep_wakeup.h's own file headers):
 *
 *   - power.h's rcp_pwrmode_t state machine, rcp_pwrmode_transition()/
 *     _wake_from_sleep(), and rcp_pwrmode_handshake_t hot-start-from-Sleep
 *     handshake.
 *   - ep_wakeup.h's SleepCMD request/response wire codec
 *     (rcp_ep_wakeup_encode_sleepcmd_request()/_decode_sleepcmd_response())
 *     and WakeUp-message codec (rcp_ep_wakeup_encode_wakeup_message()/
 *     _is_wakeup_echo()).
 *
 * rcp_powerstate_manager_t tracks, per registered rcp_avtp_addr_t endpoint
 * (replacing rcp_zone_t -- see avtp.h), a mirrored rcp_pwrmode_t and (for
 * the pin-wake hot-start path) an rcp_pwrmode_handshake_t, and supplies
 * paired encode/apply functions a caller drives around its own choice of
 * rcp_avtp_transport_t (this module sends no bytes and owns no transport
 * itself, matching udp.c/shmem.c/tsn.c's own "the transport is a distinct
 * concern" precedent from Phase 21's own Transport Satellites milestone,
 * v0.78.0):
 *
 *   - rcp_powerstate_manager_encode_entry_request() /
 *     _apply_entry_response() drive a client-initiated request that a
 *     remote RC Server enter RCP_PWRMODE_STANDBY/_SLEEP, applying
 *     power.h's own rcp_pwrmode_transition() once the matching response
 *     arrives.
 *   - rcp_powerstate_manager_wake_via_network() drives power.h's
 *     network-level wake path, requiring no wire exchange or caller-
 *     driven handshake of its own -- it synthesizes and immediately
 *     completes one internally (REQ-PWRMODE-020: power.h no longer
 *     models a network wake as handshake-free, since TC18 §12.4.1 has
 *     it "proceed as before" through the same steps a pin wake does;
 *     this function's own "always hot" contract is unchanged, only how
 *     it is achieved against power.h's corrected primitive).
 *   - rcp_powerstate_manager_handshake_begin()/_encode_wakeup_probe()/
 *     _apply_wakeup_echo()/_handshake_resume_queues()/_wake_via_pin() are
 *     thin, addr-scoped pass-throughs over power.h's own
 *     rcp_pwrmode_handshake_t step functions and ep_wakeup.h's WakeUp
 *     codec, for a caller driving the pin-wake hot-start handshake's own
 *     four steps.
 *
 * Because there is no more BusOff-equivalent fault state to recover from,
 * this module needs no background retry thread -- every action here is
 * caller-driven, and rcp_powerstate_manager_destroy() is the only
 * lifecycle function this module needs (no close(), unlike its
 * pre-replacement content and unlike watchdog.h/deadline.h's own
 * background-thread-owning Keeper/Monitor types in this same milestone).
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_POWERSTATE_H
#define RCP_POWERSTATE_H

#include "rcp/avtp.h"
#include "rcp/ep_wakeup.h"
#include "rcp/power.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_POWERSTATE_OK                   = 0,
    RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT = 1,
    RCP_POWERSTATE_ERR_DECODE           = 2, /* the underlying ep_wakeup.h decode failed */
    RCP_POWERSTATE_ERR_UNEXPECTED_TXN   = 3, /* response transaction_num didn't match the
                                                 outstanding request */
    RCP_POWERSTATE_ERR_ENTRY_REFUSED    = 4, /* decoded RCP_PWRMODE_ENTRY_REFUSED */
    RCP_POWERSTATE_ERR_TRANSITION       = 5, /* the underlying rcp_pwrmode_transition()/
                                                 _wake_from_sleep() itself rejected the move */
} rcp_powerstate_errc_t;

/* Human-readable message for an rcp_powerstate_errc_t value. Never returns NULL. */
const char *rcp_powerstate_strerror(rcp_powerstate_errc_t e);

/* ── Events ────────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_avtp_addr_t        addr;
    rcp_pwrmode_t          mode; /* the mode after this event; unchanged from before
                                     the event iff err != RCP_POWERSTATE_OK */
    rcp_powerstate_errc_t  err;  /* RCP_POWERSTATE_OK for a successful transition */
} rcp_power_event_t;

/* User-supplied callback fired on every attempted mode transition
 * (successful or not), across all registered endpoints. user_data is the
 * opaque pointer passed to rcp_powerstate_manager_subscribe(). */
typedef void (*rcp_powerstate_power_fn)(const rcp_power_event_t *ev, void *user_data);

typedef struct rcp_powerstate_manager rcp_powerstate_manager_t;

/* Creates a Manager over the given endpoints (copied by value), starting
 * every endpoint at RCP_PWRMODE_NORMAL with a not-yet-started handshake.
 * endpoints/n_endpoints may describe zero endpoints. Returns NULL on
 * allocation failure. */
rcp_powerstate_manager_t *rcp_powerstate_manager_new(const rcp_avtp_addr_t *endpoints, size_t n_endpoints);

/* Returns the current mirrored mode for addr, or RCP_PWRMODE_NORMAL if
 * addr was not registered with m (this module's own "unknown -> assume
 * the default, unremarkable state" convention -- unlike watchdog.h/
 * deadline.h's own "unknown -> assume the worst" convention, since an
 * unregistered endpoint here was never actually put in a low-power mode
 * by this Manager in the first place). */
rcp_pwrmode_t rcp_powerstate_manager_mode(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr);

/* ── Client-initiated StandBy/Sleep entry ─────────────────────────────────── */

/* Encodes a SleepCMD request asking addr to enter target_mode
 * (RCP_PWRMODE_STANDBY or RCP_PWRMODE_SLEEP), remembering target_mode and
 * transaction_num internally so a later _apply_entry_response() call can
 * apply the right transition. Returns a zeroed rcp_bytes_t (data=NULL) if
 * addr was not registered with m, if target_mode is neither StandBy nor
 * Sleep, or on allocation failure; in every such case no pending state is
 * recorded. Caller sends the returned bytes over whatever
 * rcp_avtp_transport_t (framed as an AVTPDU first, via avtp.h) it owns. */
rcp_bytes_t rcp_powerstate_manager_encode_entry_request(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                          rcp_pwrmode_t target_mode, uint8_t transaction_num);

/* Decodes a SleepCMD response b[0..len) received for addr (as produced by
 * a remote RC Server via rcp_ep_wakeup_encode_sleepcmd_response()) and, on
 * RCP_PWRMODE_ENTRY_OK, applies the previously-encoded entry request's
 * target_mode via rcp_pwrmode_transition(), firing a subscribed event
 * either way. Returns RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT if addr was not
 * registered with m; RCP_POWERSTATE_ERR_DECODE if the underlying
 * ep_wakeup.h decode failed; RCP_POWERSTATE_ERR_UNEXPECTED_TXN if the
 * decoded transaction_num doesn't match addr's outstanding request (no
 * pending request, or a stale/duplicate response -- no state changed);
 * RCP_POWERSTATE_ERR_ENTRY_REFUSED if the decoded result was
 * RCP_PWRMODE_ENTRY_REFUSED (mode unchanged); RCP_POWERSTATE_ERR_TRANSITION
 * if rcp_pwrmode_transition() itself rejected the move (mode unchanged --
 * not expected in practice, since a StandBy/Sleep request from Normal is
 * always a valid direct transition, but reported rather than asserted).
 * On RCP_POWERSTATE_OK, addr's mirrored mode is updated and its pending
 * request state is cleared. */
rcp_powerstate_errc_t rcp_powerstate_manager_apply_entry_response(rcp_powerstate_manager_t *m,
                                                                    rcp_avtp_addr_t addr,
                                                                    const uint8_t *b, size_t len);

/* ── Waking from Sleep ─────────────────────────────────────────────────────── */

/* Wakes addr from Sleep back to Normal via the always-hot network-level
 * wake path (power.h's RCP_PWRMODE_WAKE_VIA_NETWORK) -- no wire exchange
 * of this module's own is involved; see the file header.
 * *out_start_kind (if non-NULL) is always RCP_PWRMODE_START_HOT on
 * success. Returns RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT if addr was not
 * registered; RCP_POWERSTATE_ERR_TRANSITION if addr's mirrored mode isn't
 * currently RCP_PWRMODE_SLEEP. */
rcp_powerstate_errc_t rcp_powerstate_manager_wake_via_network(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                                rcp_pwrmode_start_kind_t *out_start_kind);

/* ── Pin-wake hot-start handshake (thin pass-throughs over power.h) ────────── */

/* Step (a). (Re)initializes addr's handshake with wakeup_repeat_limit and
 * attempts rcp_pwrmode_handshake_iface_reenabled() on it with the given
 * network_available (REQ-PWRMODE-016 -- see that function's own doc
 * comment in power.h). Returns false if addr was not registered with m,
 * or if network_available is false (retriable: call again once the
 * network is up; this does not consume any of wakeup_repeat_limit). */
bool rcp_powerstate_manager_handshake_begin(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                             uint32_t wakeup_repeat_limit, bool network_available);

/* Encodes one WakeUp-message probe for addr's step (b) -- a thin wrapper
 * over rcp_ep_wakeup_encode_wakeup_message(). Returns a zeroed rcp_bytes_t
 * (data=NULL) if addr was not registered with m, or on allocation
 * failure. Caller sends the returned bytes, then waits to see it (or
 * something else) echoed back. */
rcp_bytes_t rcp_powerstate_manager_encode_wakeup_probe(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                         uint8_t transaction_num);

/* Applies one step (b) attempt for addr: decodes b[0..len) via
 * rcp_ep_wakeup_is_wakeup_echo() against sent_transaction_num, then feeds
 * the result into rcp_pwrmode_handshake_wakeup_attempt() for addr's own
 * handshake. Returns false if addr was not registered with m, or per
 * rcp_pwrmode_handshake_wakeup_attempt()'s own return contract otherwise
 * (see power.h). */
bool rcp_powerstate_manager_apply_wakeup_echo(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                               const uint8_t *b, size_t len, uint8_t sent_transaction_num);

/* Step (c). A thin wrapper over rcp_pwrmode_handshake_resume_queues() for
 * addr's own handshake. Returns false if addr was not registered with m. */
bool rcp_powerstate_manager_handshake_resume_queues(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr);

/* Wakes addr from Sleep back to Normal via the pin-level wake path
 * (power.h's RCP_PWRMODE_WAKE_VIA_PIN), driven by addr's own handshake
 * state as built up by the functions above. On success, updates addr's
 * mirrored mode to RCP_PWRMODE_NORMAL and reports *out_start_kind (if
 * non-NULL) per rcp_pwrmode_wake_from_sleep()'s own hot/cold rule (see
 * power.h). Returns RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT if addr was not
 * registered; RCP_POWERSTATE_ERR_TRANSITION if addr's mirrored mode isn't
 * currently RCP_PWRMODE_SLEEP. */
rcp_powerstate_errc_t rcp_powerstate_manager_wake_via_pin(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                            rcp_pwrmode_start_kind_t *out_start_kind);

/* ── Subscription / lifecycle ─────────────────────────────────────────────── */

/* Registers cb to be invoked on every attempted mode transition, across
 * all endpoints. Not thread-safe with destroy(); register before handing
 * m to other threads. Returns false on allocation failure (cb not
 * added). */
bool rcp_powerstate_manager_subscribe(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data);

/* Frees m. No background thread to stop -- see the file header. Call
 * exactly once. */
void rcp_powerstate_manager_destroy(rcp_powerstate_manager_t *m);

#ifdef __cplusplus
}
#endif

#endif /* RCP_POWERSTATE_H */
