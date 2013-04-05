#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#define _LGPL_SOURCE 1
#include <urcu-qsbr.h>

#include "vnm.h"

// global singleton, swapped out on update by...
static vnm_db_t* db;

// this singleton updater thread
static pthread_t updater;

// Copy a str_t*'s data to a const char* in the session workspace,
//   so that after return we're not holding references to data in
//   the ndb, so that it can be swapped for update between...
static const char* vnm_str_to_vcl(struct sess* sp, const vnm_str_t* str) {
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

static void* updater_start(void* x) {
    while(1) {
        sleep(1); // XXX more like ~15-60s for the real thing, this
                 //   is to help show bugs :)
        if(1) { // XXX stat-check indicates reload necc...
            vnm_db_t* new_db = vnm_db_parse();
            if(new_db) {
                vnm_db_t* old_db = db;
                rcu_assign_pointer(db, new_db);
                synchronize_rcu();
                vnm_db_destruct(old_db);
            }
        }
    }

    return NULL;
}

static void global_fini(void) {
    // clean up the updater thread
    pthread_cancel(updater);
    pthread_join(updater, NULL);

    // free the most-recent data
    vnm_db_destruct(db);
}

static void global_init(void) {
    // initial database load
    db = vnm_db_parse();
    // start the updater thread
    pthread_create(&updater, NULL, updater_start, NULL);
}

/*****************************
 * Actual VMOD/VCL/VRT Hooks *
 *****************************/

// init-tracking
static unsigned vcl_count = 0;

static void fini_func(void* x) {
    if(!--vcl_count)
        global_fini();
}

int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
    assert(priv);

    priv->free = fini_func;
    if(!vcl_count++)
        global_init();
    return 0;
}

// Crazy hack to get per-thread rcu register/unregister, even though
//  Varnish doesn't give us per-thread hooks for the workers
//  (at least, not that I noticed...)
static pthread_key_t unreg_hack;
static pthread_once_t unreg_hack_once = PTHREAD_ONCE_INIT;
static void destruct_rcu(void* x) { pthread_setspecific(unreg_hack, NULL); rcu_unregister_thread(); }
static void make_unreg_hack(void) { pthread_key_create(&unreg_hack, destruct_rcu); }

const char* vmod_map(struct sess *sp, const char* ip_string) {
    assert(db); assert(sp); assert(ip_string);

    CHECK_OBJ_NOTNULL(sp, SESS_MAGIC); // XXX ???

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
    const vnm_str_t* str = ndb_lookup(rcu_dereference(db), ip_string);
    if(str)
        rv = vnm_str_to_vcl(sp, str);

    // normal rcu reader stuff
    rcu_read_unlock();
    rcu_thread_offline();

    return rv;
}
