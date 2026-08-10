/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/power.h"

/* ── Power modes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-001
const char *rcp_pwrmode_string(rcp_pwrmode_t mode)
{
    switch (mode) {
    case RCP_PWRMODE_NORMAL:    return "Normal";
    case RCP_PWRMODE_STANDBY:   return "StandBy";
    case RCP_PWRMODE_SLEEP:     return "Sleep";
    case RCP_PWRMODE_UNPOWERED: return "Unpowered";
    default:                    return "rcp/power: unknown mode";
    }
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-002
const char *rcp_pwrmode_strerror(rcp_pwrmode_errc_t e)
{
    switch (e) {
    case RCP_PWRMODE_OK:                     return "rcp/power: success";
    case RCP_PWRMODE_ERR_INVALID_TRANSITION: return "rcp/power: invalid power-mode transition";
    default:                                 return "rcp/power: unknown error";
    }
}

/* ── Hot vs. cold starts ──────────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-003
rcp_lifecycle_state_t rcp_pwrmode_cold_start_lifecycle_target(void)
{
    return RCP_LIFECYCLE_HW_UNCONFIGURED;
}

/* ── General mode transitions ─────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-004
rcp_pwrmode_errc_t rcp_pwrmode_transition(rcp_pwrmode_t *mode, rcp_pwrmode_t target,
                                           rcp_pwrmode_start_kind_t *out_start_kind)
{
    rcp_pwrmode_start_kind_t kind;

    if (*mode == target) {
        kind = RCP_PWRMODE_START_HOT;
    } else if ((*mode == RCP_PWRMODE_NORMAL && target == RCP_PWRMODE_STANDBY) ||
               (*mode == RCP_PWRMODE_STANDBY && target == RCP_PWRMODE_NORMAL)) {
        kind = RCP_PWRMODE_START_HOT;
    } else if ((*mode == RCP_PWRMODE_NORMAL && target == RCP_PWRMODE_SLEEP) ||
               (*mode == RCP_PWRMODE_STANDBY && target == RCP_PWRMODE_SLEEP)) {
        kind = RCP_PWRMODE_START_COLD;
    } else if (target == RCP_PWRMODE_UNPOWERED &&
               (*mode == RCP_PWRMODE_NORMAL || *mode == RCP_PWRMODE_STANDBY || *mode == RCP_PWRMODE_SLEEP)) {
        kind = RCP_PWRMODE_START_COLD;
    } else if (*mode == RCP_PWRMODE_UNPOWERED && target == RCP_PWRMODE_NORMAL) {
        kind = RCP_PWRMODE_START_COLD;
    } else {
        /* Covers, among others, SLEEP -> NORMAL (use
         * rcp_pwrmode_wake_from_sleep() instead), StandBy<->Sleep
         * directly, and Unpowered<->StandBy/Sleep directly. */
        return RCP_PWRMODE_ERR_INVALID_TRANSITION;
    }

    *mode = target;
    if (out_start_kind) *out_start_kind = kind;
    return RCP_PWRMODE_OK;
}

/* ── Waking from Sleep ─────────────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-005
//cfusa:req REQ-PWRMODE-020
bool rcp_pwrmode_hotstart_required(rcp_pwrmode_wake_path_t path)
{
    /* REQ-PWRMODE-020 (TC18 §12.4.1): a TC14/TC10 network wake "will
     * directly check for the network availability and proceed as
     * before" -- the same hot-start handshake a pin/EP-signal wake runs,
     * not a skip. path is intentionally unused now (kept in the
     * signature as a future extensibility hook -- see this function's
     * own header doc comment). */
    (void)path;
    return true;
}

//cfusa:req REQ-PWRMODE-006
void rcp_pwrmode_handshake_init(rcp_pwrmode_handshake_t *hs, uint32_t repeat_limit)
{
    hs->step                 = RCP_PWRMODE_HANDSHAKE_NOT_STARTED;
    hs->wakeup_attempts      = 0u;
    hs->wakeup_repeat_limit  = repeat_limit;
}

//cfusa:req REQ-PWRMODE-007
//cfusa:req REQ-PWRMODE-016
bool rcp_pwrmode_handshake_iface_reenabled(rcp_pwrmode_handshake_t *hs, bool network_available)
{
    if (hs->step != RCP_PWRMODE_HANDSHAKE_NOT_STARTED) return false;
    /* REQ-PWRMODE-016 (TC18 §12.4.1): the interface is enabled, then
     * network availability is checked, before any WakeUp message is
     * sent -- a caller polls this again (a cheap, uncounted "not yet")
     * until network_available is true, only then does step (b)'s own
     * wakeup_repeat_limit-bounded budget start being spent. */
    if (!network_available) return false;

    hs->step = RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED;
    return true;
}

//cfusa:req REQ-PWRMODE-008
bool rcp_pwrmode_handshake_wakeup_attempt(rcp_pwrmode_handshake_t *hs, bool echoed)
{
    if (hs->step != RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED) return false;

    hs->wakeup_attempts++;

    if (echoed) {
        hs->step = RCP_PWRMODE_HANDSHAKE_ECHOED;
        return true;
    }

    if (hs->wakeup_attempts >= hs->wakeup_repeat_limit) {
        hs->step = RCP_PWRMODE_HANDSHAKE_FAILED;
        return false;
    }

    return true; /* still pending; caller repeats */
}

//cfusa:req REQ-PWRMODE-009
bool rcp_pwrmode_handshake_resume_queues(rcp_pwrmode_handshake_t *hs)
{
    if (hs->step != RCP_PWRMODE_HANDSHAKE_ECHOED) return false;

    hs->step = RCP_PWRMODE_HANDSHAKE_COMPLETE;
    return true;
}

//cfusa:req REQ-PWRMODE-010
bool rcp_pwrmode_handshake_is_complete(const rcp_pwrmode_handshake_t *hs)
{
    return hs->step == RCP_PWRMODE_HANDSHAKE_COMPLETE;
}

//cfusa:req REQ-PWRMODE-011
bool rcp_pwrmode_handshake_has_failed(const rcp_pwrmode_handshake_t *hs)
{
    return hs->step == RCP_PWRMODE_HANDSHAKE_FAILED;
}

//cfusa:req REQ-PWRMODE-012
rcp_pwrmode_errc_t rcp_pwrmode_wake_from_sleep(rcp_pwrmode_t *mode, rcp_pwrmode_wake_path_t path,
                                                const rcp_pwrmode_handshake_t *handshake,
                                                rcp_pwrmode_start_kind_t *out_start_kind)
{
    rcp_pwrmode_start_kind_t kind;

    if (*mode != RCP_PWRMODE_SLEEP) return RCP_PWRMODE_ERR_INVALID_TRANSITION;

    if (!rcp_pwrmode_hotstart_required(path)) {
        kind = RCP_PWRMODE_START_HOT;
    } else if (handshake != NULL && rcp_pwrmode_handshake_is_complete(handshake)) {
        kind = RCP_PWRMODE_START_HOT;
    } else {
        kind = RCP_PWRMODE_START_COLD;
    }

    *mode = RCP_PWRMODE_NORMAL;
    if (out_start_kind) *out_start_kind = kind;
    return RCP_PWRMODE_OK;
}

/* ── Entry-refusal gating ─────────────────────────────────────────────────── */

//cfusa:req REQ-PWRMODE-013
rcp_pwrmode_entry_result_t rcp_pwrmode_check_entry(const rcp_pwrmode_entry_gate_t *gate)
{
    if (!gate) return RCP_PWRMODE_ENTRY_REFUSED;

    if (!gate->wup_status_clear || !gate->endpoint_idle || !gate->response_queue_empty)
        return RCP_PWRMODE_ENTRY_REFUSED;

    return RCP_PWRMODE_ENTRY_OK;
}
