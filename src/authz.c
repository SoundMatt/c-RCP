/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/authz.h"
#include "rcp/alloc.h"

#include "alloc_overflow.h"
#include "mem_bounded.h"
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
    rcp_strncpy_bounded(dst, RCP_AUTHZ_IDENTITY_MAX, src);
}

//cfusa:req REQ-AUTH-009
rcp_authz_policy_t *rcp_authz_policy_new(void)
{
    rcp_authz_policy_t *p = (rcp_authz_policy_t *)rcp_calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->refcount = 1;
    rcp_mutex_init(&p->mu);
    return p;
}

//cfusa:req REQ-AUTH-010
//cfusa:req REQ-AUTH-012
rcp_authz_policy_t *rcp_authz_policy_retain(rcp_authz_policy_t *p)
{
    if (p) rcp_atomic_inc(&p->refcount);
    return p;
}

static void entry_free(policy_entry_t *e)
{
    rcp_free(e->addrs);
    e->addrs = NULL;
    rcp_free(e->request_types);
    e->request_types = NULL;
}

//cfusa:req REQ-AUTH-011
//cfusa:req REQ-AUTH-013
void rcp_authz_policy_release(rcp_authz_policy_t *p)
{
    size_t i;

    if (!p) return;
    if (rcp_atomic_dec(&p->refcount) > 0) return;
    for (i = 0; i < p->entries_len; i++) entry_free(&p->entries[i]);
    rcp_mutex_destroy(&p->mu);
    rcp_free(p->entries);
    p->entries = NULL;
    rcp_free(p);
    p = NULL;
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
        size_t alloc_bytes = rcp_alloc_checked_size(n_addrs, sizeof(*entry.addrs));
        entry.addrs = alloc_bytes == 0 ? NULL : (rcp_avtp_addr_t *)rcp_malloc(alloc_bytes);
        if (!entry.addrs) return false;
        rcp_memcpy_bounded(entry.addrs, alloc_bytes, addrs, n_addrs * sizeof(*entry.addrs));
        entry.n_addrs = n_addrs;
    }
    if (n_request_types > 0) {
        size_t alloc_bytes = rcp_alloc_checked_size(n_request_types, sizeof(*entry.request_types));
        entry.request_types = alloc_bytes == 0 ? NULL : (uint8_t *)rcp_malloc(alloc_bytes);
        if (!entry.request_types) {
            rcp_free(entry.addrs);
            entry.addrs = NULL;
            return false;
        }
        rcp_memcpy_bounded(entry.request_types, alloc_bytes, request_types, n_request_types * sizeof(*entry.request_types));
        entry.n_request_types = n_request_types;
    }

    rcp_mutex_lock(&policy->mu);
    if (policy->entries_len == policy->entries_cap) {
        size_t new_cap = (policy->entries_cap == 0) ? 4 : policy->entries_cap * 2;
        size_t alloc_bytes = rcp_alloc_checked_size(new_cap, sizeof(*policy->entries));
        policy_entry_t *grown = alloc_bytes == 0
            ? NULL
            : (policy_entry_t *)rcp_realloc(policy->entries, alloc_bytes);
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
