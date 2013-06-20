#ifndef VNM_STRDB_HDR
#define VNM_STRDB_HDR

typedef struct {
    unsigned len; // includes NUL in length
    char* data; // NUL-terminated
} vnm_str_t;

struct _vnm_strdb;
typedef struct _vnm_strdb vnm_strdb_t;

vnm_strdb_t* vnm_strdb_new(void);
unsigned vnm_strdb_add(vnm_strdb_t* d, const char* str);
const vnm_str_t* vnm_strdb_get(const vnm_strdb_t* d, const unsigned idx);
void vnm_strdb_destroy(vnm_strdb_t* d);

#endif // VNM_STRDB_HDR
