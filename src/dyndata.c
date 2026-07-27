#include "rcp/dyndata.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

void rcp_schema_entry_free(rcp_schema_entry_t *e)
{
    if (!e->fields) return;
    free(e->fields);
    e->fields     = NULL;
    e->fields_len = 0;
}

struct rcp_schema_registry {
    rcp_mutex_t          mu; /* protects entries[] */
    rcp_schema_entry_t  *entries;
    size_t                len;
    size_t                cap;
};

rcp_schema_registry_t *rcp_schema_registry_new(void)
{
    rcp_schema_registry_t *reg = (rcp_schema_registry_t *)calloc(1, sizeof(*reg));
    if (!reg) return NULL;
    rcp_mutex_init(&reg->mu);
    return reg;
}

void rcp_schema_registry_destroy(rcp_schema_registry_t *reg)
{
    size_t i;

    if (!reg) return;
    for (i = 0; i < reg->len; i++) {
        rcp_schema_entry_free(&reg->entries[i]);
    }
    rcp_mutex_destroy(&reg->mu);
    free(reg->entries);
    free(reg);
}

static rcp_field_descriptor_t *dup_fields(const rcp_field_descriptor_t *fields, size_t fields_len)
{
    rcp_field_descriptor_t *copy;

    if (fields_len == 0) return NULL;
    copy = (rcp_field_descriptor_t *)malloc(fields_len * sizeof(*copy));
    if (!copy) return NULL;
    memcpy(copy, fields, fields_len * sizeof(*copy));
    return copy;
}

//cfusa:req REQ-DYN-001
//cfusa:req REQ-DYN-006
int rcp_schema_registry_add(rcp_schema_registry_t *reg, rcp_schema_id_t id, const char *name,
                             const rcp_field_descriptor_t *fields, size_t fields_len)
{
    rcp_field_descriptor_t *fields_copy;
    size_t i;

    rcp_mutex_lock(&reg->mu);
    for (i = 0; i < reg->len; i++) {
        if (reg->entries[i].id == id) {
            rcp_mutex_unlock(&reg->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }

    if (fields_len > 0) {
        fields_copy = dup_fields(fields, fields_len);
        if (!fields_copy) {
            rcp_mutex_unlock(&reg->mu);
            return RCP_ERR_BUSY;
        }
    } else {
        fields_copy = NULL;
    }

    if (reg->len == reg->cap) {
        size_t new_cap = (reg->cap == 0) ? 8 : reg->cap * 2;
        rcp_schema_entry_t *grown = (rcp_schema_entry_t *)realloc(reg->entries, new_cap * sizeof(*grown));
        if (!grown) {
            free(fields_copy);
            rcp_mutex_unlock(&reg->mu);
            return RCP_ERR_BUSY;
        }
        reg->entries = grown;
        reg->cap     = new_cap;
    }

    reg->entries[reg->len].id         = id;
    strncpy(reg->entries[reg->len].name, name, sizeof(reg->entries[reg->len].name) - 1);
    reg->entries[reg->len].name[sizeof(reg->entries[reg->len].name) - 1] = '\0';
    reg->entries[reg->len].fields     = fields_copy;
    reg->entries[reg->len].fields_len = fields_len;
    reg->len++;

    rcp_mutex_unlock(&reg->mu);
    return RCP_OK;
}

//cfusa:req REQ-DYN-002
bool rcp_schema_registry_lookup(rcp_schema_registry_t *reg, rcp_schema_id_t id, rcp_schema_entry_t *out)
{
    size_t i;

    rcp_mutex_lock(&reg->mu);
    for (i = 0; i < reg->len; i++) {
        if (reg->entries[i].id == id) {
            const rcp_schema_entry_t *src = &reg->entries[i];
            out->id         = src->id;
            memcpy(out->name, src->name, sizeof(out->name));
            out->fields_len = src->fields_len;
            out->fields     = dup_fields(src->fields, src->fields_len);
            rcp_mutex_unlock(&reg->mu);
            return true;
        }
    }
    rcp_mutex_unlock(&reg->mu);
    return false;
}

size_t rcp_schema_registry_size(rcp_schema_registry_t *reg)
{
    size_t n;
    rcp_mutex_lock(&reg->mu);
    n = reg->len;
    rcp_mutex_unlock(&reg->mu);
    return n;
}

//cfusa:req REQ-DYN-003
//cfusa:req REQ-DYN-004
rcp_bytes_t rcp_dynamic_payload_encode(const rcp_dynamic_payload_t *dp)
{
    rcp_bytes_t out = {0};
    size_t data_len = dp->data.len;
    uint32_t schema_id = dp->schema_id;
    uint8_t *buf;

    buf = (uint8_t *)malloc(4 + data_len);
    if (!buf) return out;

    buf[0] = (uint8_t)(schema_id >> 24);
    buf[1] = (uint8_t)(schema_id >> 16);
    buf[2] = (uint8_t)(schema_id >> 8);
    buf[3] = (uint8_t)(schema_id);
    if (data_len > 0) memcpy(buf + 4, dp->data.data, data_len);

    out.data = buf;
    out.len  = 4 + data_len;
    return out;
}

//cfusa:req REQ-DYN-004
//cfusa:req REQ-DYN-005
rcp_dynamic_payload_t rcp_dynamic_payload_decode(const uint8_t *raw, size_t raw_len)
{
    rcp_dynamic_payload_t dp = {0};

    if (raw_len < 4) return dp;

    dp.schema_id = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
                 | ((uint32_t)raw[2] << 8)  |  (uint32_t)raw[3];
    dp.data = rcp_bytes_dup(raw + 4, raw_len - 4);
    return dp;
}
