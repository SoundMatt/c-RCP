/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/powerstate.h"

#include "platform.h"

#include <stdlib.h>

//cfusa:req REQ-PWR-010
const char *rcp_powerstate_strerror(rcp_powerstate_errc_t e)
{
    switch (e) {
    case RCP_POWERSTATE_OK:                   return "ok";
    case RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT:  return "unknown endpoint";
    case RCP_POWERSTATE_ERR_DECODE:            return "decode failed";
    case RCP_POWERSTATE_ERR_UNEXPECTED_TXN:    return "unexpected transaction number";
    case RCP_POWERSTATE_ERR_ENTRY_REFUSED:     return "entry refused";
    case RCP_POWERSTATE_ERR_TRANSITION:        return "invalid transition";
    default:                                   return "unknown";
    }
}

typedef struct {
    rcp_avtp_addr_t         addr;
    rcp_pwrmode_t           mode;
    rcp_pwrmode_handshake_t handshake;
    /* REQ-PWRMODE-017: the responder stream configured for the original
     * standby/sleep request, recorded by rcp_powerstate_manager_
     * handshake_begin(). Meaningless (all-zero) until has_resp_stream_id
     * is set -- distinguished explicitly rather than relying on an
     * all-zero rcp_stream_id_t sentinel, since an all-zero StreamID is
     * not itself provably invalid. */
    rcp_stream_id_t         resp_stream_id;
    bool                    has_resp_stream_id;

    bool          request_pending;
    rcp_pwrmode_t pending_target;
    uint8_t       pending_txn;
} endpoint_entry_t;

struct rcp_powerstate_manager {
    rcp_mutex_t               mu; /* protects entries[], callbacks[] */
    endpoint_entry_t         *entries;
    size_t                    n_entries;
    rcp_powerstate_power_fn  *callbacks;
    void                    **callback_ctx;
    size_t                    n_callbacks;
    size_t                    callbacks_cap;
};

static endpoint_entry_t *find_entry(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr)
{
    size_t i;
    for (i = 0; i < m->n_entries; i++) {
        if (rcp_avtp_addr_equal(m->entries[i].addr, addr)) return &m->entries[i];
    }
    return NULL;
}

static bool callbacks_append(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data)
{
    if (m->n_callbacks == m->callbacks_cap) {
        size_t new_cap = (m->callbacks_cap == 0) ? 4 : m->callbacks_cap * 2;
        rcp_powerstate_power_fn *grown_cb = (rcp_powerstate_power_fn *)realloc(m->callbacks, new_cap * sizeof(*grown_cb));
        void                   **grown_ctx;
        if (!grown_cb) return false;
        m->callbacks = grown_cb;
        grown_ctx = (void **)realloc(m->callback_ctx, new_cap * sizeof(*grown_ctx));
        if (!grown_ctx) return false;
        m->callback_ctx  = grown_ctx;
        m->callbacks_cap = new_cap;
    }
    m->callbacks[m->n_callbacks]    = cb;
    m->callback_ctx[m->n_callbacks] = user_data;
    m->n_callbacks++;
    return true;
}

static void emit(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr, rcp_pwrmode_t mode, rcp_powerstate_errc_t err)
{
    rcp_power_event_t ev;
    size_t i;

    ev.addr = addr;
    ev.mode = mode;
    ev.err  = err;

    /* Invoked outside any lock the caller might be holding when it calls
     * into this module: subscribe() is documented as not thread-safe with
     * destroy(), matching watchdog.c/deadline.c's own precedent, so
     * n_callbacks/callbacks are safe to read here without mu (only ever
     * appended to before m is handed to other threads). */
    for (i = 0; i < m->n_callbacks; i++) {
        m->callbacks[i](&ev, m->callback_ctx[i]);
    }
}

//cfusa:req REQ-PWR-011
rcp_powerstate_manager_t *rcp_powerstate_manager_new(const rcp_avtp_addr_t *endpoints, size_t n_endpoints)
{
    rcp_powerstate_manager_t *m = (rcp_powerstate_manager_t *)calloc(1, sizeof(*m));
    size_t i;

    if (!m) return NULL;
    rcp_mutex_init(&m->mu);

    if (n_endpoints > 0) {
        endpoint_entry_t *entries = (endpoint_entry_t *)calloc(n_endpoints, sizeof(*entries));
        m->entries = entries;
        if (!m->entries) {
            rcp_mutex_destroy(&m->mu);
            free(m);
            return NULL;
        }
    }
    for (i = 0; i < n_endpoints; i++) {
        m->entries[i].addr            = endpoints[i];
        m->entries[i].mode            = RCP_PWRMODE_NORMAL;
        m->entries[i].request_pending = false;
        rcp_pwrmode_handshake_init(&m->entries[i].handshake, 0);
    }
    m->n_entries = n_endpoints;

    return m;
}

//cfusa:req REQ-PWR-012
rcp_pwrmode_t rcp_powerstate_manager_mode(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr)
{
    endpoint_entry_t *e;
    rcp_pwrmode_t mode;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    mode = e ? e->mode : RCP_PWRMODE_NORMAL;
    rcp_mutex_unlock(&m->mu);
    return mode;
}

//cfusa:req REQ-PWR-001
rcp_bytes_t rcp_powerstate_manager_encode_entry_request(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                          rcp_pwrmode_t target_mode, uint8_t transaction_num)
{
    endpoint_entry_t *e;
    rcp_bytes_t frame;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    rcp_mutex_unlock(&m->mu);
    if (!e) {
        rcp_bytes_t zero = {0};
        return zero;
    }

    frame = rcp_ep_wakeup_encode_sleepcmd_request(addr.byte_bus_id, target_mode, transaction_num);
    if (frame.data) {
        rcp_mutex_lock(&m->mu);
        e->request_pending = true;
        e->pending_target  = target_mode;
        e->pending_txn     = transaction_num;
        rcp_mutex_unlock(&m->mu);
    }
    return frame;
}

//cfusa:req REQ-PWR-002
//cfusa:req REQ-PWR-003
//cfusa:req REQ-PWR-004
rcp_powerstate_errc_t rcp_powerstate_manager_apply_entry_response(rcp_powerstate_manager_t *m,
                                                                    rcp_avtp_addr_t addr,
                                                                    const uint8_t *b, size_t len)
{
    endpoint_entry_t *e;
    rcp_pwrmode_entry_result_t result;
    uint8_t txn;
    rcp_ep_wakeup_errc_t dec;
    rcp_pwrmode_t target;
    rcp_pwrmode_errc_t tec;
    rcp_pwrmode_t mode_after;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    rcp_mutex_unlock(&m->mu);
    if (!e) return RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT;

    dec = rcp_ep_wakeup_decode_sleepcmd_response(b, len, addr.byte_bus_id, &result, &txn);
    if (dec != RCP_EP_WAKEUP_OK) return RCP_POWERSTATE_ERR_DECODE;

    rcp_mutex_lock(&m->mu);
    if (!e->request_pending || e->pending_txn != txn) {
        rcp_mutex_unlock(&m->mu);
        return RCP_POWERSTATE_ERR_UNEXPECTED_TXN;
    }
    target = e->pending_target;
    rcp_mutex_unlock(&m->mu);

    if (result == RCP_PWRMODE_ENTRY_REFUSED) {
        rcp_mutex_lock(&m->mu);
        e->request_pending = false;
        mode_after = e->mode; /* unchanged on refusal -- see the header's
                                  "unchanged from before the event iff
                                  err != RCP_POWERSTATE_OK" contract */
        rcp_mutex_unlock(&m->mu);
        emit(m, addr, mode_after, RCP_POWERSTATE_ERR_ENTRY_REFUSED);
        return RCP_POWERSTATE_ERR_ENTRY_REFUSED;
    }

    rcp_mutex_lock(&m->mu);
    tec = rcp_pwrmode_transition(&e->mode, target, NULL);
    e->request_pending = false;
    mode_after = e->mode;
    rcp_mutex_unlock(&m->mu);

    if (tec != RCP_PWRMODE_OK) {
        emit(m, addr, mode_after, RCP_POWERSTATE_ERR_TRANSITION);
        return RCP_POWERSTATE_ERR_TRANSITION;
    }

    emit(m, addr, mode_after, RCP_POWERSTATE_OK);
    return RCP_POWERSTATE_OK;
}

//cfusa:req REQ-PWR-005
rcp_powerstate_errc_t rcp_powerstate_manager_wake_via_network(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                                rcp_pwrmode_start_kind_t *out_start_kind)
{
    endpoint_entry_t *e;
    rcp_pwrmode_errc_t ec;
    rcp_pwrmode_t mode_after;
    rcp_pwrmode_handshake_t hs;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT;
    }
    /* REQ-PWRMODE-020: power.h's rcp_pwrmode_hotstart_required() no
     * longer special-cases a network wake as always-hot-with-no-
     * handshake (primary-source correction: TC18 §12.4.1 has a network
     * wake "proceed as before", i.e. run the same handshake a pin/EP-
     * signal wake does). This function's own documented contract (see
     * powerstate.h) is "always hot for a network wake" -- preserved here
     * by driving a synthetic, immediately-completed handshake locally:
     * this wrapper represents the whole network wake-up event (network
     * already available, WakeUp already answered by construction of a
     * *network*-sourced wake) happening atomically, not a caller-driven
     * multi-step exchange the way the pin-wake path below is. */
    rcp_pwrmode_handshake_init(&hs, 1u);
    /* Network availability is trivially true here: the wake-up signal
     * arrived over the network in the first place (REQ-PWRMODE-016's
     * own precondition is already satisfied by construction). */
    rcp_pwrmode_handshake_iface_reenabled(&hs, true);
    rcp_pwrmode_handshake_wakeup_attempt(&hs, true);
    rcp_pwrmode_handshake_resume_queues(&hs);
    ec = rcp_pwrmode_wake_from_sleep(&e->mode, RCP_PWRMODE_WAKE_VIA_NETWORK, &hs, out_start_kind);
    mode_after = e->mode;
    rcp_mutex_unlock(&m->mu);

    if (ec != RCP_PWRMODE_OK) {
        emit(m, addr, mode_after, RCP_POWERSTATE_ERR_TRANSITION);
        return RCP_POWERSTATE_ERR_TRANSITION;
    }

    emit(m, addr, mode_after, RCP_POWERSTATE_OK);
    return RCP_POWERSTATE_OK;
}

//cfusa:req REQ-PWR-006
//cfusa:req REQ-PWRMODE-016
//cfusa:req REQ-PWRMODE-017
bool rcp_powerstate_manager_handshake_begin(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                             uint32_t wakeup_repeat_limit, bool network_available,
                                             rcp_stream_id_t resp_stream_id)
{
    endpoint_entry_t *e;
    bool ok;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return false;
    }
    rcp_pwrmode_handshake_init(&e->handshake, wakeup_repeat_limit);
    /* REQ-PWRMODE-017: recorded regardless of network_available below --
     * a caller retrying this call once the network comes up should not
     * have to re-supply the same resp_stream_id, and recording it here
     * costs nothing even on the calls that don't yet advance the
     * handshake itself. */
    e->resp_stream_id     = resp_stream_id;
    e->has_resp_stream_id = true;
    /* REQ-PWRMODE-016: network_available is this caller's own
     * already-classified answer (e.g. BEACONs detected by the PHY) --
     * see rcp_pwrmode_handshake_iface_reenabled()'s own doc comment. A
     * caller whose network is not yet available calls this again once
     * it is; this module never polls hardware itself. */
    ok = rcp_pwrmode_handshake_iface_reenabled(&e->handshake, network_available);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-PWRMODE-017
bool rcp_powerstate_manager_wake_response_stream_id(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                     rcp_stream_id_t *out_resp_stream_id)
{
    endpoint_entry_t *e;
    bool has;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    has = e && e->has_resp_stream_id;
    if (has && out_resp_stream_id) *out_resp_stream_id = e->resp_stream_id;
    rcp_mutex_unlock(&m->mu);
    return has;
}

//cfusa:req REQ-PWR-013
rcp_bytes_t rcp_powerstate_manager_encode_wakeup_probe(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                         uint8_t transaction_num)
{
    endpoint_entry_t *e;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    rcp_mutex_unlock(&m->mu);
    if (!e) {
        rcp_bytes_t zero = {0};
        return zero;
    }

    return rcp_ep_wakeup_encode_wakeup_message(addr.byte_bus_id, transaction_num);
}

//cfusa:req REQ-PWR-007
bool rcp_powerstate_manager_apply_wakeup_echo(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                               const uint8_t *b, size_t len, uint8_t sent_transaction_num)
{
    endpoint_entry_t *e;
    bool echoed;
    bool ok;

    /* rcp_ep_wakeup_is_wakeup_echo() is a pure function over b/len -- safe
     * to call before taking mu. */
    echoed = rcp_ep_wakeup_is_wakeup_echo(b, len, addr.byte_bus_id, sent_transaction_num);

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return false;
    }
    ok = rcp_pwrmode_handshake_wakeup_attempt(&e->handshake, echoed);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-PWR-014
bool rcp_powerstate_manager_handshake_resume_queues(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr)
{
    endpoint_entry_t *e;
    bool ok;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return false;
    }
    ok = rcp_pwrmode_handshake_resume_queues(&e->handshake);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-PWR-008
rcp_powerstate_errc_t rcp_powerstate_manager_wake_via_pin(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr,
                                                            rcp_pwrmode_start_kind_t *out_start_kind)
{
    endpoint_entry_t *e;
    rcp_pwrmode_errc_t ec;
    rcp_pwrmode_t mode_after;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, addr);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT;
    }
    ec = rcp_pwrmode_wake_from_sleep(&e->mode, RCP_PWRMODE_WAKE_VIA_PIN, &e->handshake, out_start_kind);
    mode_after = e->mode;
    rcp_mutex_unlock(&m->mu);

    if (ec != RCP_PWRMODE_OK) {
        emit(m, addr, mode_after, RCP_POWERSTATE_ERR_TRANSITION);
        return RCP_POWERSTATE_ERR_TRANSITION;
    }

    emit(m, addr, mode_after, RCP_POWERSTATE_OK);
    return RCP_POWERSTATE_OK;
}

//cfusa:req REQ-PWR-009
bool rcp_powerstate_manager_subscribe(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data)
{
    bool ok;
    rcp_mutex_lock(&m->mu);
    ok = callbacks_append(m, cb, user_data);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-PWR-015
void rcp_powerstate_manager_destroy(rcp_powerstate_manager_t *m)
{
    if (!m) return;

    free(m->entries);
    free(m->callbacks);
    free(m->callback_ctx);
    rcp_mutex_destroy(&m->mu);
    free(m);
}
