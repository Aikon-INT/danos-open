/*
 * DANOS-Open Management: gNMI Protobuf Codec (v0.3)
 *
 * Real protobuf (proto3) binary wire-format codec for the gNMI service
 * subset, per openconfig/gnmi proto/gnmi/gnmi.proto:
 *
 *   service gNMI { Capabilities, Get, Set, Subscribe }
 *   package gnmi
 *
 * Field numbers verified against gnmi.proto master:
 *   CapabilityRequest { ext=1 }
 *   CapabilityResponse { supported_models=1, supported_encodings=2,
 *                        gNMI_version=3 }
 *   ModelData { name=1, organization=2, version=3 }
 *   GetRequest { prefix=1, path=2, type=3, encoding=5, use_models=6 }
 *   GetResponse { notification=1 }
 *   SetRequest { prefix=1, delete=2, replace=3, update=4 }
 *   SetResponse { prefix=1, response=2, timestamp=4 }
 *   UpdateResult { timestamp=1, path=2, message=3, op=4 }
 *   Notification { timestamp=1, prefix=2, update=4, delete=5, atomic=6 }
 *   Update { path=1, val=3, duplicates=4 }
 *   Path { element=1(deprecated), origin=2, elem=3, target=4 }
 *   PathElem { name=1, key=2 (map<string,string>) }
 *   TypedValue oneof: string_val=1, int_val=2, uint_val=3, bool_val=4,
 *     bytes_val=5, float_val=6, decimal_val=7, leaflist_val=8, any_val=9,
 *     json_val=10, json_ietf_val=11, ascii_val=12, proto_bytes=13,
 *     double_val=14
 *   Encoding enum: JSON=0, BYTES=1, PROTO=2, ASCII=3, JSON_IETF=4
 *   DataType: ALL=0, CONFIG=1, STATE=2, OPERATIONAL=3
 *   Operation: INVALID=0, DELETE=1, REPLACE=2, UPDATE=3
 */

#ifndef DANOS_GNMI_PROTO_H__
#define DANOS_GNMI_PROTO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- limits ---------------------------------------------------------- */
#define GNMI_MAX_ELEMS      8
#define GNMI_MAX_UPDATES    16
#define GNMI_MAX_NAME       64
#define GNMI_MAX_VAL        256

/* ---- model types ------------------------------------------------------ */

typedef struct {
    char     name[GNMI_MAX_NAME];
    bool     has_key;
    char     key_name[GNMI_MAX_NAME];
    char     key_value[GNMI_MAX_NAME];
} gnmi_path_elem_t;

typedef struct {
    char             origin[32];
    uint32_t         elem_count;
    gnmi_path_elem_t elems[GNMI_MAX_ELEMS];
} gnmi_path_t;

typedef enum {
    GNMI_VAL_UNSET = 0,
    GNMI_VAL_STRING = 1,
    GNMI_VAL_INT = 2,
    GNMI_VAL_UINT = 3,
    GNMI_VAL_BOOL = 4,
    GNMI_VAL_JSON = 10,
    GNMI_VAL_JSON_IETF = 11,
    GNMI_VAL_ASCII = 12,
} gnmi_val_kind_t;

typedef struct {
    gnmi_val_kind_t kind;
    char    s[GNMI_MAX_VAL];   /* string/json/ascii */
    int64_t i;
    uint64_t u;
    bool    b;
} gnmi_typed_value_t;

typedef struct {
    gnmi_path_t       path;
    gnmi_typed_value_t val;
} gnmi_update_t;

typedef struct {
    uint64_t       timestamp;
    char           prefix_origin[32];
    uint32_t       update_count;
    gnmi_update_t  updates[GNMI_MAX_UPDATES];
} gnmi_notification_t;

typedef enum { GNMI_OP_UPDATE = 3, GNMI_OP_REPLACE = 2, GNMI_OP_DELETE = 1 } gnmi_op_t;

typedef enum { GNMI_ENC_JSON = 0, GNMI_ENC_BYTES = 1, GNMI_ENC_PROTO = 2,
               GNMI_ENC_ASCII = 3, GNMI_ENC_JSON_IETF = 4 } gnmi_encoding_t;

/* ---- protobuf primitives ---------------------------------------------- */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;      /* set when a write exceeded cap */
} gnmi_pb_t;

void gnmi_pb_init(gnmi_pb_t *w, void *buf, size_t cap);
void gnmi_pb_put_varint(gnmi_pb_t *w, uint64_t v);
void gnmi_pb_put_tag(gnmi_pb_t *w, uint32_t field, uint32_t wire);
void gnmi_pb_put_len_delim(gnmi_pb_t *w, uint32_t field,
                           const void *data, size_t len);
void gnmi_pb_put_string(gnmi_pb_t *w, uint32_t field, const char *s);
void gnmi_pb_put_uint64(gnmi_pb_t *w, uint32_t field, uint64_t v);
void gnmi_pb_put_bool(gnmi_pb_t *w, uint32_t field, bool v);
void gnmi_pb_put_enum(gnmi_pb_t *w, uint32_t field, int32_t v);
/* Begin a nested message: returns saved length position to restore */
size_t gnmi_pb_begin_nested(gnmi_pb_t *w, uint32_t field);
void   gnmi_pb_end_nested(gnmi_pb_t *w, size_t saved);

/* Reader */
typedef struct {
    const uint8_t *p;
    size_t   len;
    size_t   pos;
    bool     err;
} gnmi_pb_reader_t;

void     gnmi_pbr_init(gnmi_pb_reader_t *r, const void *buf, size_t len);
uint64_t gnmi_pbr_varint(gnmi_pb_reader_t *r);
/* Read one tag; returns field number (0 = end/error), sets *wire */
uint32_t gnmi_pbr_tag(gnmi_pb_reader_t *r, uint32_t *wire);
bool     gnmi_pbr_bytes(gnmi_pb_reader_t *r, const uint8_t **data, size_t *len);
/* Skip a field value of the given wire type */
void     gnmi_pbr_skip(gnmi_pb_reader_t *r, uint32_t wire);

/* ---- message codec ----------------------------------------------------- */

/* Path: encode into writer as field `field` */
void gnmi_encode_path(gnmi_pb_t *w, uint32_t field, const gnmi_path_t *path);
/* Decode a Path from bytes (already length-delimited) */
bool gnmi_decode_path(const uint8_t *data, size_t len, gnmi_path_t *out);

void gnmi_encode_typed_value(gnmi_pb_t *w, uint32_t field,
                             const gnmi_typed_value_t *val);
bool gnmi_decode_typed_value(const uint8_t *data, size_t len,
                             gnmi_typed_value_t *out);

/* GetRequest */
typedef struct {
    gnmi_path_t     prefix;
    uint32_t        path_count;
    gnmi_path_t     paths[GNMI_MAX_ELEMS];
    int32_t         data_type;      /* DataType */
    gnmi_encoding_t encoding;
} gnmi_get_request_t;

bool gnmi_decode_get_request(const uint8_t *data, size_t len,
                             gnmi_get_request_t *out);
bool gnmi_encode_get_response(gnmi_pb_t *w,
                              const gnmi_notification_t *notif,
                              uint32_t notif_count);

/* SetRequest */
typedef struct {
    gnmi_path_t    prefix;
    uint32_t       delete_count;
    gnmi_path_t    deletes[GNMI_MAX_ELEMS];
    uint32_t       update_count;
    gnmi_update_t  updates[GNMI_MAX_UPDATES];
    uint32_t       replace_count;
    gnmi_update_t  replaces[GNMI_MAX_UPDATES];
} gnmi_set_request_t;

bool gnmi_decode_set_request(const uint8_t *data, size_t len,
                             gnmi_set_request_t *out);

/* SetResponse with one UpdateResult per operation */
typedef struct {
    gnmi_path_t  prefix;
    gnmi_path_t  result_paths[GNMI_MAX_UPDATES];
    gnmi_op_t    ops[GNMI_MAX_UPDATES];
    uint32_t     result_count;
    uint64_t     timestamp;
} gnmi_set_response_t;

bool gnmi_encode_set_response(gnmi_pb_t *w, const gnmi_set_response_t *resp);

/* Subscribe (v0.4)
 * SubscribeRequest oneof: subscribe=1, poll=3
 * SubscriptionList { prefix=1, subscription=2, mode=5, encoding=8, updates_only=9 }
 * Subscription { path=1 }
 * Mode: STREAM=0, ONCE=1, POLL=2
 * SubscribeResponse oneof: update=1 (Notification), sync_response=3 (bool)
 */
typedef enum { GNMI_SUB_MODE_STREAM = 0, GNMI_SUB_MODE_ONCE = 1,
               GNMI_SUB_MODE_POLL = 2 } gnmi_sub_mode_t;

typedef struct {
    gnmi_path_t path;
} gnmi_subscription_t;

typedef struct {
    gnmi_path_t          prefix;
    uint32_t             sub_count;
    gnmi_subscription_t  subs[GNMI_MAX_ELEMS];
    gnmi_sub_mode_t      mode;
    gnmi_encoding_t      encoding;
    bool                 updates_only;
} gnmi_subscribe_list_t;

typedef struct {
    gnmi_subscribe_list_t subscribe;
    bool is_poll;
} gnmi_subscribe_request_t;

bool gnmi_decode_subscribe_request(const uint8_t *data, size_t len,
                                   gnmi_subscribe_request_t *out);

/* Encode SubscribeResponse { update = Notification } */
bool gnmi_encode_subscribe_update(gnmi_pb_t *w, const gnmi_notification_t *n);
/* Encode SubscribeResponse { sync_response = true } */
bool gnmi_encode_subscribe_sync(gnmi_pb_t *w);

/* Capabilities */
typedef struct {
    const char *name;
    const char *organization;
    const char *version;
} gnmi_model_data_t;
bool gnmi_encode_capabilities_response(gnmi_pb_t *w,
                                       const gnmi_model_data_t *models,
                                       uint32_t model_count,
                                       const gnmi_encoding_t *encodings,
                                       uint32_t encoding_count,
                                       const char *gnmi_version);

/* Notification helpers */
void gnmi_encode_notification(gnmi_pb_t *w, uint32_t field,
                              const gnmi_notification_t *n);

/* Convenience: build a path with dotted notation "a/b/c[k=v]" */
bool gnmi_path_from_str(gnmi_path_t *p, const char *dotted);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_GNMI_PROTO_H__ */
