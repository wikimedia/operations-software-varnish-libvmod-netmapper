#ifndef VNM_HDR
#define VNM_HDR

typedef struct {
    unsigned len; // includes NUL
    char* data; // NUL-terminated
} vnm_str_t;

vnm_str_t* vnm_str_new(const char* input);
void vnm_str_destroy(vnm_str_t* str);

typedef struct _vnm_db_struct vnm_db_t;

vnm_db_t* vnm_db_parse(void);
void vnm_db_destruct(vnm_db_t* n);
const vnm_str_t* ndb_lookup(const vnm_db_t* d, const char* ip_string);

#endif // VNM_HDR
