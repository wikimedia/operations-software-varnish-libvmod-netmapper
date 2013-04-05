#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <jansson.h>

#define _LGPL_SOURCE 1
#include <urcu-qsbr.h>

// XXX this whole thing needs error retval checking on lib calls, etc...

typedef struct {
    unsigned len; // includes NUL
    char* data; // NUL-terminated
} str_t;

str_t* str_new(const char* input) {
    assert(input);
    str_t* rv = malloc(sizeof(str_t));
    rv->len = strlen(input) + 1;
    rv->data = malloc(rv->len);
    memcpy(rv->data, input, rv->len);
    return rv;
}

void str_destroy(str_t* str) {
    free(str->data);
    free(str);
}

// Copy a str_t*'s data to a const char* in the session workspace,
//   so that after return we're not holding references to data in
//   the ndb, so that it can be swapped for update between...
static const char* str_to_vcl(struct sess* sp, const str_t* str) {
    const char* rv = NULL;
    unsigned used = 0;
    const unsigned space = WS_Reserve(sp->ws, 0);
    if(space < str->len) {
        // XXX not SLT_LostHeader, what?
        WSP(sp, SLT_LostHeader, "vmod_netmapper: no space for string retval!");
    }
    else {
        used = str->len;
        rv = sp->ws->f;
	    memcpy(sp->ws->f, str->data, used);
    }
    WS_Release(sp->ws, used);
    return rv;
}

/* ndb_t */

static pthread_t ndb_updater;

typedef struct {
    // XXX TODO: flesh this out
    str_t* the_only_one;
} ndb_t;

// global singleton, swapped out on update
static ndb_t* ndb;

static void ndb_destruct(ndb_t* n) {
    str_destroy(n->the_only_one);
    free(n);
}

static ndb_t* ndb_parse(void) {

    /* XXX TODO: actually parse JSON into a real datastructure...
    json_error_t errobj;
    json_t* toplevel = json_load_file("/home/bblack/test.json", 0, &errobj);
    */
 
    ndb_t* n = malloc(sizeof(ndb_t));
    n->the_only_one = str_new("001-01");
    return n; // XXX or NULL if parse fails
}

static void* ndb_updater_start(void* x) {
    while(1) {
        sleep(1); // XXX more like ~15-60s for the real thing, this
                 //   is to help show bugs :)
        if(1) { // XXX stat-check indicates reload necc...
            ndb_t* new_db = ndb_parse();
            if(new_db) {
                ndb_t* old_db = ndb;
                rcu_assign_pointer(ndb, new_db);
                synchronize_rcu();
                ndb_destruct(old_db);
            }
        }
    }

    return NULL;
}

static void ndb_fini(void) {
    // clean up the updater thread
    pthread_cancel(ndb_updater);
    pthread_join(ndb_updater, NULL);

    // free the most-recent data
    ndb_destruct(ndb);
}

static void ndb_init(void) {
    // initial database load
    ndb = ndb_parse();
    // start the updater thread
    pthread_create(&ndb_updater, NULL, ndb_updater_start, NULL);
}

static const str_t* ndb_lookup(const char* addr_str) {
    // important!
    ndb_t* cur_db = rcu_dereference(ndb);

    // XXX actually look up a specific string
    //   based on the text address in addr_str
    return cur_db->the_only_one;
}

// Crazy hack to get per-thread rcu register/unregister, even though
//  Varnish doesn't give us per-thread hooks for the workers
//  (at least, not that I noticed...)
static pthread_key_t unreg_hack;
static pthread_once_t unreg_hack_once = PTHREAD_ONCE_INIT;
static void destruct_rcu(void* x) { pthread_setspecific(unreg_hack, NULL); rcu_unregister_thread(); }
static void make_unreg_hack(void) { pthread_key_create(&unreg_hack, destruct_rcu); }

static const char* ndb_map(struct sess* sp, const char* ip_string) {
    assert(ndb); assert(sp); assert(ip_string);

    // The rest of the rcu register/unregister hack
    static __thread bool rcu_registered = false;
    if(!rcu_registered) {
        pthread_once(&unreg_hack_once, make_unreg_hack);
	pthread_setspecific(unreg_hack, (void*)1);
	rcu_register_thread();
        rcu_registered = true;
    }

    const char* rv = NULL;

    // normal rcu reader stuff
    rcu_thread_online();
    rcu_read_lock();

    // search net database.  if match, convert
    //  string to a vcl string and return it...
    const str_t* str = ndb_lookup(ip_string);
    if(str)
        rv = str_to_vcl(sp, str);

    // normal rcu reader stuff
    rcu_read_unlock();
    rcu_thread_offline();

    return rv;
}

/*****************************
 * Actual VMOD/VCL/VRT Hooks *
 *****************************/

// init-tracking
static unsigned vcl_count = 0;

static void fini_func(void* x) {
    if(!--vcl_count)
        ndb_fini();
}

int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
    assert(priv);

    priv->free = fini_func;
    if(!vcl_count++)
        ndb_init();
    return 0;
}

const char* vmod_map(struct sess *sp, const char* ip_string) {
    assert(sp);

    CHECK_OBJ_NOTNULL(sp, SESS_MAGIC); // XXX ???
    return ndb_map(sp, ip_string);
}
