#ifndef VNM_HDR
#define VNM_HDR

#include "vnm_strdb.h"

typedef struct _vnm_db_struct vnm_db_t;

vnm_db_t* vnm_db_parse(const char* fn);
void vnm_db_destruct(vnm_db_t* n);
const vnm_str_t* vnm_lookup(const vnm_db_t* d, const char* ip_string);

#endif // VNM_HDR
