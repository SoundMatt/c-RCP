#include "rcp/authz.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ── AccessPolicy ──────────────────────────────────────────────────────────── */

typedef struct {
    char     identity[RCP_AUTHZ_IDENTITY_MAX];
    uint32_t zone_mask;     /* bit (1u << zone) set if allowed; 0 = all zones */
    uint32_t cmd_type_mask; /* bit (1u << type) set if allowed; 0 = all types */
} policy_entry_t;

struct rcp_authz_policy {
    int              refcount;
    rcp_mutex_t       mu; /* protects entries[] */
    policy_entry_t   *entries;
    size_t            entries_len;
    size_t            entries_cap;
};

static void copy_identity(char *dst, const char *src)
{
    strncpy(dst, src, RCP_AUTHZ_IDENTITY_MAX - 1);
    dst[RCP_AUTHZ_IDENTITY_MAX - 1] = '\0';
}

rcp_authz_policy_t *rcp_authz_policy_new(void)
{
    rcp_authz_policy_t *p = (rcp_authz_policy_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->refcount = 1;
    rcp_mutex_init(&p->mu);
    return p;
}

rcp_authz_policy_t *rcp_authz_policy_retain(rcp_authz_policy_t *p)
{
    if (p) rcp_atomic_inc(&p->refcount);
    return p;
}

void rcp_authz_policy_release(rcp_authz_policy_t *p)
{
    if (!p) return;
    if (rcp_atomic_dec(&p->refcount) > 0) return;
    rcp_mutex_destroy(&p->mu);
    free(p->entries);
    free(p);
}

//cfusa:req REQ-AUTH-003
bool rcp_authz_policy_allow(rcp_authz_policy_t *policy, const char *identity,
                             const rcp_zone_t *zones, size_t n_zones,
                             const rcp_command_type_t *cmd_types, size_t n_cmd_types)
{
    policy_entry_t entry;
    size_t i;
    bool ok = true;

    memset(&entry, 0, sizeof(entry));
    copy_identity(entry.identity, identity);
    for (i = 0; i < n_zones; i++) entry.zone_mask |= (1u << (unsigned)zones[i]);
    for (i = 0; i < n_cmd_types; i++) entry.cmd_type_mask |= (1u << (unsigned)cmd_types[i]);

    rcp_mutex_lock(&policy->mu);
    if (policy->entries_len == policy->entries_cap) {
        size_t new_cap = (policy->entries_cap == 0) ? 4 : policy->entries_cap * 2;
        policy_entry_t *grown = (policy_entry_t *)realloc(policy->entries, new_cap * sizeof(*grown));
        if (!grown) {
            ok = false;
        } else {
            policy->entries     = grown;
            policy->entries_cap = new_cap;
        }
    }
    if (ok) policy->entries[policy->entries_len++] = entry;
    rcp_mutex_unlock(&policy->mu);
    return ok;
}

//cfusa:req REQ-AUTH-001
//cfusa:req REQ-AUTH-002
//cfusa:req REQ-AUTH-004
bool rcp_authz_policy_permit(rcp_authz_policy_t *policy, const char *identity,
                              rcp_zone_t zone, rcp_command_type_t type)
{
    size_t i;
    bool permitted = false;

    rcp_mutex_lock(&policy->mu);
    for (i = 0; i < policy->entries_len; i++) {
        const policy_entry_t *e = &policy->entries[i];
        bool zone_ok;
        bool type_ok;

        if (strncmp(e->identity, identity, RCP_AUTHZ_IDENTITY_MAX) != 0) continue;

        zone_ok = (e->zone_mask == 0)     || (e->zone_mask & (1u << (unsigned)zone));
        type_ok = (e->cmd_type_mask == 0) || (e->cmd_type_mask & (1u << (unsigned)type));
        if (zone_ok && type_ok) {
            permitted = true;
            break;
        }
    }
    rcp_mutex_unlock(&policy->mu);
    return permitted;
}

/* ── AuthController ────────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t       base;
    rcp_controller_t      *inner;  /* retained */
    rcp_authz_policy_t     *policy; /* retained */
    rcp_authz_identity_fn  identity_fn;
    void                   *identity_fn_user_data;
    rcp_mutex_t              mu; /* protects fixed_identity */
    char                     fixed_identity[RCP_AUTHZ_IDENTITY_MAX];
} authz_controller_t;

static rcp_zone_t authz_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((authz_controller_t *)self)->inner);
}

//cfusa:req REQ-AUTH-001
//cfusa:req REQ-AUTH-002
static int authz_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                            const rcp_command_t *cmd, rcp_response_t *out)
{
    authz_controller_t *ac = (authz_controller_t *)self;
    const char *id;
    char id_buf[RCP_AUTHZ_IDENTITY_MAX];

    if (ac->identity_fn) {
        id = ac->identity_fn(ac->identity_fn_user_data);
    } else {
        rcp_mutex_lock(&ac->mu);
        copy_identity(id_buf, ac->fixed_identity);
        rcp_mutex_unlock(&ac->mu);
        id = id_buf;
    }

    if (!rcp_authz_policy_permit(ac->policy, id, cmd->zone, cmd->type)) {
        return RCP_ERR_FORBIDDEN;
    }
    return rcp_controller_send(ac->inner, ctx, cmd, out);
}

//cfusa:req REQ-AUTH-006
static int authz_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    authz_controller_t *ac = (authz_controller_t *)self;
    return rcp_controller_subscribe(ac->inner, ctx, out);
}

//cfusa:req REQ-AUTH-007
static int authz_ctrl_close(rcp_controller_t *self)
{
    authz_controller_t *ac = (authz_controller_t *)self;
    return rcp_controller_close(ac->inner);
}

static void authz_ctrl_destroy(rcp_controller_t *self)
{
    authz_controller_t *ac = (authz_controller_t *)self;
    rcp_controller_release(ac->inner);
    rcp_authz_policy_release(ac->policy);
    rcp_mutex_destroy(&ac->mu);
    free(ac);
}

static const rcp_controller_vtable_t authz_controller_vtable = {
    authz_ctrl_zone,
    authz_ctrl_send,
    authz_ctrl_subscribe,
    authz_ctrl_close,
    authz_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_authz_controller_new(rcp_controller_t *inner, rcp_authz_policy_t *policy,
                                            rcp_authz_identity_fn identity_fn, void *identity_fn_user_data)
{
    authz_controller_t *ac = (authz_controller_t *)calloc(1, sizeof(*ac));
    if (!ac) return NULL;
    ac->base.vt                 = &authz_controller_vtable;
    ac->base.refcount            = 1;
    ac->inner                    = rcp_controller_retain(inner);
    ac->policy                   = rcp_authz_policy_retain(policy);
    ac->identity_fn               = identity_fn;
    ac->identity_fn_user_data     = identity_fn_user_data;
    rcp_mutex_init(&ac->mu);
    return &ac->base;
}

void rcp_authz_controller_set_identity(rcp_controller_t *ctrl, const char *identity)
{
    authz_controller_t *ac = (authz_controller_t *)ctrl;
    rcp_mutex_lock(&ac->mu);
    copy_identity(ac->fixed_identity, identity);
    rcp_mutex_unlock(&ac->mu);
}
