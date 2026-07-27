#include "rcp/federation.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>
#include <string.h>

typedef struct {
    rcp_zone_t         zone;
    rcp_controller_t  *ctrl; /* registry holds one reference */
} fed_local_entry_t;

typedef struct {
    rcp_zone_t         zone;
    char                owner[RCP_FED_HPC_ID_MAX];
    uint64_t            expires_at_ms;
    rcp_controller_t   *remote_ctrl; /* registry holds one reference */
} fed_lease_entry_t;

typedef struct {
    rcp_registry_t       base;
    char                    local_id[RCP_FED_HPC_ID_MAX];
    rcp_mutex_t              mu;
    bool                     closed;
    fed_local_entry_t      *local_entries;
    size_t                   local_len;
    size_t                   local_cap;
    fed_lease_entry_t      *lease_entries;
    size_t                   lease_len;
    size_t                   lease_cap;
} fed_registry_t;

static void copy_id(char *dst, const char *src)
{
    strncpy(dst, src, RCP_FED_HPC_ID_MAX - 1);
    dst[RCP_FED_HPC_ID_MAX - 1] = '\0';
}

//cfusa:req REQ-FED-008
static int fed_registry_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    fed_registry_t *fr = (fed_registry_t *)self;
    rcp_zone_t        zone = rcp_controller_zone(ctrl);
    size_t             i;

    rcp_mutex_lock(&fr->mu);
    if (fr->closed) {
        rcp_mutex_unlock(&fr->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < fr->local_len; i++) {
        if (fr->local_entries[i].zone == zone) {
            rcp_mutex_unlock(&fr->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }
    if (fr->local_len == fr->local_cap) {
        size_t new_cap = (fr->local_cap == 0) ? 8 : fr->local_cap * 2;
        fed_local_entry_t *grown = (fed_local_entry_t *)realloc(fr->local_entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&fr->mu);
            return RCP_ERR_BUSY;
        }
        fr->local_entries = grown;
        fr->local_cap     = new_cap;
    }
    fr->local_entries[fr->local_len].zone = zone;
    fr->local_entries[fr->local_len].ctrl = rcp_controller_retain(ctrl);
    fr->local_len++;
    rcp_mutex_unlock(&fr->mu);
    return RCP_OK;
}

//cfusa:req REQ-FED-010
static int fed_registry_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    fed_registry_t   *fr = (fed_registry_t *)self;
    rcp_controller_t *found = NULL;
    size_t              i;

    rcp_mutex_lock(&fr->mu);
    for (i = 0; i < fr->local_len; i++) {
        if (fr->local_entries[i].zone == zone) {
            found = fr->local_entries[i].ctrl;
            fr->local_entries[i] = fr->local_entries[fr->local_len - 1];
            fr->local_len--;
            break;
        }
    }
    rcp_mutex_unlock(&fr->mu);

    if (!found) return RCP_ERR_NOT_FOUND;

    rcp_controller_close(found);
    rcp_controller_release(found);
    return RCP_OK;
}

//cfusa:req REQ-FED-001
//cfusa:req REQ-FED-002
//cfusa:req REQ-FED-004
//cfusa:req REQ-FED-007
static int fed_registry_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    fed_registry_t *fr = (fed_registry_t *)self;
    size_t            i;

    rcp_mutex_lock(&fr->mu);
    if (fr->closed) {
        rcp_mutex_unlock(&fr->mu);
        return RCP_ERR_CLOSED;
    }

    for (i = 0; i < fr->local_len; i++) {
        if (fr->local_entries[i].zone == zone) {
            *out = rcp_controller_retain(fr->local_entries[i].ctrl);
            rcp_mutex_unlock(&fr->mu);
            return RCP_OK;
        }
    }

    for (i = 0; i < fr->lease_len; i++) {
        if (fr->lease_entries[i].zone == zone) {
            if (rcp_monotonic_ms() >= fr->lease_entries[i].expires_at_ms) {
                rcp_mutex_unlock(&fr->mu);
                return RCP_ERR_NOT_FOUND;
            }
            *out = rcp_controller_retain(fr->lease_entries[i].remote_ctrl);
            rcp_mutex_unlock(&fr->mu);
            return RCP_OK;
        }
    }

    rcp_mutex_unlock(&fr->mu);
    return RCP_ERR_NOT_FOUND;
}

//cfusa:req REQ-FED-009
static size_t fed_registry_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    fed_registry_t *fr = (fed_registry_t *)self;
    size_t            i, n;

    rcp_mutex_lock(&fr->mu);
    n = fr->local_len;
    for (i = 0; i < n && i < cap; i++) {
        out[i] = rcp_controller_retain(fr->local_entries[i].ctrl);
    }
    rcp_mutex_unlock(&fr->mu);
    return n;
}

//cfusa:req REQ-FED-006
static int fed_registry_close(rcp_registry_t *self)
{
    fed_registry_t     *fr = (fed_registry_t *)self;
    bool                   was_open;
    fed_local_entry_t    *local = NULL;
    size_t                 local_len = 0;
    fed_lease_entry_t    *leases = NULL;
    size_t                 lease_len = 0;
    size_t                 i;

    rcp_mutex_lock(&fr->mu);
    was_open = !fr->closed;
    if (was_open) {
        fr->closed        = true;
        local             = fr->local_entries;
        local_len         = fr->local_len;
        fr->local_entries = NULL;
        fr->local_len     = 0;
        fr->local_cap     = 0;
        leases            = fr->lease_entries;
        lease_len         = fr->lease_len;
        fr->lease_entries = NULL;
        fr->lease_len     = 0;
        fr->lease_cap     = 0;
    }
    rcp_mutex_unlock(&fr->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_controller_close(local[i].ctrl);
        rcp_controller_release(local[i].ctrl);
    }
    free(local);
    for (i = 0; i < lease_len; i++) {
        rcp_controller_close(leases[i].remote_ctrl);
        rcp_controller_release(leases[i].remote_ctrl);
    }
    free(leases);
    return RCP_OK;
}

static void fed_registry_destroy(rcp_registry_t *self)
{
    fed_registry_t *fr = (fed_registry_t *)self;
    (void)fed_registry_close(self); /* idempotent; releases any remaining refs */
    rcp_mutex_destroy(&fr->mu);
    free(fr->local_entries); /* NULL after close(); freeing NULL is a no-op */
    free(fr->lease_entries);
    free(fr);
}

static const rcp_registry_vtable_t federation_registry_vtable = {
    fed_registry_register,
    fed_registry_deregister,
    fed_registry_lookup,
    fed_registry_controllers,
    fed_registry_close,
    fed_registry_destroy,
};

rcp_registry_t *rcp_federation_registry_new(const char *local_id)
{
    fed_registry_t *fr = (fed_registry_t *)calloc(1, sizeof(*fr));
    if (!fr) return NULL;
    fr->base.vt = &federation_registry_vtable;
    copy_id(fr->local_id, local_id);
    rcp_mutex_init(&fr->mu);
    return &fr->base;
}

//cfusa:req REQ-FED-005
const char *rcp_federation_registry_local_id(rcp_registry_t *reg)
{
    return ((fed_registry_t *)reg)->local_id;
}

int rcp_federation_registry_add_lease(rcp_registry_t *reg, rcp_zone_t zone, const char *owner,
                                       uint64_t expires_at_ms, rcp_controller_t *remote_ctrl)
{
    fed_registry_t *fr = (fed_registry_t *)reg;
    size_t            i;

    rcp_mutex_lock(&fr->mu);
    if (fr->closed) {
        rcp_mutex_unlock(&fr->mu);
        return RCP_ERR_CLOSED;
    }

    for (i = 0; i < fr->lease_len; i++) {
        if (fr->lease_entries[i].zone == zone) {
            rcp_controller_release(fr->lease_entries[i].remote_ctrl);
            copy_id(fr->lease_entries[i].owner, owner);
            fr->lease_entries[i].expires_at_ms = expires_at_ms;
            fr->lease_entries[i].remote_ctrl   = rcp_controller_retain(remote_ctrl);
            rcp_mutex_unlock(&fr->mu);
            return RCP_OK;
        }
    }

    if (fr->lease_len == fr->lease_cap) {
        size_t new_cap = (fr->lease_cap == 0) ? 8 : fr->lease_cap * 2;
        fed_lease_entry_t *grown = (fed_lease_entry_t *)realloc(fr->lease_entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&fr->mu);
            return RCP_ERR_BUSY;
        }
        fr->lease_entries = grown;
        fr->lease_cap     = new_cap;
    }
    fr->lease_entries[fr->lease_len].zone = zone;
    copy_id(fr->lease_entries[fr->lease_len].owner, owner);
    fr->lease_entries[fr->lease_len].expires_at_ms = expires_at_ms;
    fr->lease_entries[fr->lease_len].remote_ctrl   = rcp_controller_retain(remote_ctrl);
    fr->lease_len++;
    rcp_mutex_unlock(&fr->mu);
    return RCP_OK;
}

//cfusa:req REQ-FED-003
int rcp_federation_registry_revoke_lease(rcp_registry_t *reg, rcp_zone_t zone)
{
    fed_registry_t   *fr = (fed_registry_t *)reg;
    rcp_controller_t *found = NULL;
    size_t              i;

    rcp_mutex_lock(&fr->mu);
    for (i = 0; i < fr->lease_len; i++) {
        if (fr->lease_entries[i].zone == zone) {
            found = fr->lease_entries[i].remote_ctrl;
            fr->lease_entries[i] = fr->lease_entries[fr->lease_len - 1];
            fr->lease_len--;
            break;
        }
    }
    rcp_mutex_unlock(&fr->mu);

    if (found) rcp_controller_release(found);
    return RCP_OK;
}
