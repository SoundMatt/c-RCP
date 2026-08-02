/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/authz.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char             identity[RCP_AUTHZ_IDENTITY_MAX];
    rcp_avtp_addr_t *addrs;         /* NULL/0 = any address */
    size_t           n_addrs;
    uint8_t         *request_types; /* NULL/0 = any request type */
    size_t           n_request_types;
} policy_entry_t;

struct rcp_authz_policy {
    int              refcount;
    rcp_mutex_t      mu; /* protects entries[] */
    policy_entry_t  *entries;
    size_t           entries_len;
    size_t           entries_cap;
};

static void copy_identity(char *dst, const char *src)
{
    strncpy(dst, src, RCP_AUTHZ_IDENTITY_MAX - 1);
    dst[RCP_AUTHZ_IDENTITY_MAX - 1] = '\0';
}

//cfusa:req REQ-AUTH-009
rcp_authz_policy_t *rcp_authz_policy_new(void)
{
    rcp_authz_policy_t *p = (rcp_authz_policy_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->refcount = 1;
    rcp_mutex_init(&p->mu);
    return p;
}

//cfusa:req REQ-AUTH-010
rcp_authz_policy_t *rcp_authz_policy_retain(rcp_authz_policy_t *p)
{
    if (p) rcp_atomic_inc(&p->refcount);
    return p;
}

static void entry_free(policy_entry_t *e)
{
    free(e->addrs);
    free(e->request_types);
}

//cfusa:req REQ-AUTH-011
void rcp_authz_policy_release(rcp_authz_policy_t *p)
{
    size_t i;

    if (!p) return;
    if (rcp_atomic_dec(&p->refcount) > 0) return;
    for (i = 0; i < p->entries_len; i++) entry_free(&p->entries[i]);
    rcp_mutex_destroy(&p->mu);
    free(p->entries);
    free(p);
}

//cfusa:req REQ-AUTH-003
//cfusa:req REQ-AUTH-008
bool rcp_authz_policy_allow(rcp_authz_policy_t *policy, const char *identity,
                             const rcp_avtp_addr_t *addrs, size_t n_addrs,
                             const uint8_t *request_types, size_t n_request_types)
{
    policy_entry_t entry;
    bool ok = true;

    memset(&entry, 0, sizeof(entry));
    copy_identity(entry.identity, identity);

    if (n_addrs > 0) {
        entry.addrs = (rcp_avtp_addr_t *)malloc(n_addrs * sizeof(*entry.addrs));
        if (!entry.addrs) return false;
        memcpy(entry.addrs, addrs, n_addrs * sizeof(*entry.addrs));
        entry.n_addrs = n_addrs;
    }
    if (n_request_types > 0) {
        entry.request_types = (uint8_t *)malloc(n_request_types * sizeof(*entry.request_types));
        if (!entry.request_types) {
            free(entry.addrs);
            return false;
        }
        memcpy(entry.request_types, request_types, n_request_types * sizeof(*entry.request_types));
        entry.n_request_types = n_request_types;
    }

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
    if (ok) {
        policy->entries[policy->entries_len++] = entry;
    }
    rcp_mutex_unlock(&policy->mu);

    if (!ok) entry_free(&entry);
    return ok;
}

//cfusa:req REQ-AUTH-005
static bool entry_matches_addr(const policy_entry_t *e, rcp_avtp_addr_t addr)
{
    size_t i;

    if (e->n_addrs == 0) return true;
    for (i = 0; i < e->n_addrs; i++) {
        if (rcp_avtp_addr_equal(e->addrs[i], addr)) return true;
    }
    return false;
}

//cfusa:req REQ-AUTH-006
static bool entry_matches_request_type(const policy_entry_t *e, uint8_t request_type)
{
    size_t i;

    if (e->n_request_types == 0) return true;
    for (i = 0; i < e->n_request_types; i++) {
        if (e->request_types[i] == request_type) return true;
    }
    return false;
}

//cfusa:req REQ-AUTH-001
//cfusa:req REQ-AUTH-002
//cfusa:req REQ-AUTH-004
//cfusa:req REQ-AUTH-007
bool rcp_authz_policy_permit(rcp_authz_policy_t *policy, const char *identity,
                              rcp_avtp_addr_t addr, uint8_t request_type)
{
    size_t i;
    bool permitted = false;

    rcp_mutex_lock(&policy->mu);
    for (i = 0; i < policy->entries_len; i++) {
        const policy_entry_t *e = &policy->entries[i];

        if (strncmp(e->identity, identity, RCP_AUTHZ_IDENTITY_MAX) != 0) continue;
        if (entry_matches_addr(e, addr) && entry_matches_request_type(e, request_type)) {
            permitted = true;
            break;
        }
    }
    rcp_mutex_unlock(&policy->mu);
    return permitted;
}
