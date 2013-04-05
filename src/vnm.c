#include "vnm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

vnm_str_t* vnm_str_new(const char* input) {
    assert(input);
    vnm_str_t* rv = malloc(sizeof(vnm_str_t));
    rv->len = strlen(input) + 1;
    rv->data = malloc(rv->len);
    memcpy(rv->data, input, rv->len);
    return rv;
}

void vnm_str_destroy(vnm_str_t* str) {
    free(str->data);
    free(str);
}

struct _vnm_db_struct {
    // XXX TODO: flesh this out
    vnm_str_t* the_only_one;
};

void vnm_db_destruct(vnm_db_t* n) {
    vnm_str_destroy(n->the_only_one);
    free(n);
}

vnm_db_t* vnm_db_parse(void) {
    /* XXX TODO: actually parse JSON into a real datastructure...

    json_error_t errobj;
    json_t* toplevel = json_load_file("/home/bblack/test.json", 0, &errobj);
    */
    vnm_db_t* d = malloc(sizeof(vnm_db_t));
    d->the_only_one = vnm_str_new("001-01");
    return d; // XXX or NULL if parse fails
}

const vnm_str_t* ndb_lookup(const vnm_db_t* d, const char* ip_string) {
    // XXX actually look up a specific string
    //   based on the text address in addr_str
    return d->the_only_one;
}
