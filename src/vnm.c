#include "vnm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

// XXX hack for now, not sure what to do with this stuff here, yet...
#define ERR(fmt,...) fprintf(stderr, fmt, ##__VA_ARGS__)

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
    json_error_t errobj;
    json_t* toplevel = json_load_file("/home/bblack/test.json", 0, &errobj);

    if(!toplevel) {
        ERR("Failed to load JSON input: %s", errobj.text);
        return NULL;
    }

    if(!json_is_object(toplevel)) {
        ERR("JSON input is not an object!");
        return NULL;
    }

    // use this to pre-alloc?
    const unsigned num_keys = json_object_size(toplevel);

    // iterate the keys...
    const char* key;
    json_t* val;
    void *iter = json_object_iter(toplevel);
    while(iter) {
        key = json_object_iter_key(iter);
        val = json_object_iter_value(iter);
        // XXX insert "key" as a new vnm_str_t
        if(!json_is_array(val)) {
            ERR("JSON value for key '%s' should be an array!", key);
            return NULL;
        }
        const unsigned nnets = json_array_size(val);
        for(unsigned i = 0; i < nnets; i++) {
            const json_t* net = json_array_get(val, i);
            if(!json_is_string(net)) {
                ERR("JSON array member %u for key '%s' should be an address string!", i, key);
                return NULL;
            }
            // XXX insert data from "net" into database, referencing "key"
        }
        iter = json_object_iter_next(toplevel, iter);
    }

    // clean up json
    json_decref(toplevel);

    vnm_db_t* d = malloc(sizeof(vnm_db_t));
    d->the_only_one = vnm_str_new("001-01");
    return d; // XXX or NULL if parse fails
}

const vnm_str_t* ndb_lookup(const vnm_db_t* d, const char* ip_string) {
    // XXX actually look up a specific string
    //   based on the text address in addr_str
    return d->the_only_one;
}
