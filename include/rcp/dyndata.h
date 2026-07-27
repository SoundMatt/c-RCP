/*
 * Runtime schema registry and dynamic payload encoding.
 *
 * rcp_dynamic_payload_t is a self-describing envelope: a 4-byte schema ID
 * followed by the encoded value blob. rcp_schema_registry_t maps schema
 * IDs to human-readable names and optional field descriptors.
 */
#ifndef RCP_DYNDATA_H
#define RCP_DYNDATA_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t rcp_schema_id_t;

typedef struct {
    char     name[32];
    char     type[16]; /* "uint8"/"uint16"/"uint32"/"float"/"bytes" */
    uint32_t offset;
    uint32_t size;
} rcp_field_descriptor_t;

typedef struct {
    rcp_schema_id_t          id;
    char                       name[64];
    rcp_field_descriptor_t  *fields; /* owned; free via rcp_schema_entry_free() */
    size_t                    fields_len;
} rcp_schema_entry_t;

/* Frees e->fields and zeroes the fields/fields_len members. Safe to call
 * on an already-freed or zero-initialized rcp_schema_entry_t. */
void rcp_schema_entry_free(rcp_schema_entry_t *e);

typedef struct rcp_schema_registry rcp_schema_registry_t;

/* Returns NULL on allocation failure. */
rcp_schema_registry_t *rcp_schema_registry_new(void);

/* Frees every stored entry's fields array and the registry itself. Call
 * exactly once. */
void rcp_schema_registry_destroy(rcp_schema_registry_t *reg);

/* Adds a schema entry under id, deep-copying name and fields[0..fields_len)
 * into the registry's own storage (the caller retains ownership of the
 * fields array passed in). fields may be NULL iff fields_len == 0. Returns
 * RCP_ERR_ALREADY_EXISTS if id is already registered. Thread-safe. */
int rcp_schema_registry_add(rcp_schema_registry_t *reg, rcp_schema_id_t id, const char *name,
                             const rcp_field_descriptor_t *fields, size_t fields_len);

/* On success, deep-copies the entry registered under id into *out (which
 * the caller must eventually release with rcp_schema_entry_free()) and
 * returns true. Returns false without modifying *out if id was never
 * added. Thread-safe. */
bool rcp_schema_registry_lookup(rcp_schema_registry_t *reg, rcp_schema_id_t id, rcp_schema_entry_t *out);

size_t rcp_schema_registry_size(rcp_schema_registry_t *reg);

typedef struct {
    rcp_schema_id_t schema_id;
    rcp_bytes_t       data; /* borrowed when passed to rcp_dynamic_payload_encode();
                              * owned (must rcp_bytes_free()) when returned by
                              * rcp_dynamic_payload_decode() */
} rcp_dynamic_payload_t;

/* Packs dp->schema_id (big-endian) followed by dp->data into a new owned
 * rcp_bytes_t (4 + dp->data.len bytes) -- the caller must eventually call
 * rcp_bytes_free() on the result. dp->data is only read, never freed or
 * retained. */
rcp_bytes_t rcp_dynamic_payload_encode(const rcp_dynamic_payload_t *dp);

/* Parses a wire payload into a rcp_dynamic_payload_t whose data is an
 * owned copy (the caller must eventually call rcp_bytes_free() on
 * out->data). If raw_len < 4, returns { schema_id = 0, data = {NULL, 0} }
 * rather than reading out of bounds or signalling an error -- matching
 * cpp-RCP's own deliberately lenient decode() (verified by its own test
 * suite, not a bug). */
rcp_dynamic_payload_t rcp_dynamic_payload_decode(const uint8_t *raw, size_t raw_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DYNDATA_H */
