/* Copyright © 2013 Brandon L Black <bblack@wikimedia.org>
 *
 * This file is part of libvmod-netmapper.
 *
 * libvmod-netmapper is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libvmod-netmapper is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libvmod-netmapper.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include <pthread.h>
#define _LGPL_SOURCE 1
#include <urcu-qsbr.h>

#include "vnm.h"

typedef struct {
    unsigned reload_check_interval;
    char* fn;
    vnm_db_t* db;
    pthread_t updater;
    struct stat db_stat;
} vnm_priv_t;

// Copy a str_t*'s data to a const char* in the session workspace,
//   so that after return we're not holding references to data in
//   the vnm db, so that it can be swapped for update between...
static const char* vnm_str_to_vcl(struct sess* sp, const vnm_str_t* str) {
    const char* rv = NULL;
    if(str->data) {
        unsigned used = 0;
        const unsigned space = WS_Reserve(sp->ws, 0);
        if(space < str->len) {
            WSP(sp, SLT_Error, "vmod_netmapper: no space for string retval!");
        }
        else {
            used = str->len;
            rv = sp->ws->f;
            memcpy(sp->ws->f, str->data, used);
        }
        WS_Release(sp->ws, used);
    }
    return rv;
}

static void* updater_start(void* vp_asvoid) {
    vnm_priv_t* vp = vp_asvoid;
    struct stat check_stat;

    while(1) {
        sleep(vp->reload_check_interval);
        if(stat(vp->fn, &check_stat)) {
            VSL(SLT_Error, 0, "vmod_netmapper: Failed to stat JSON database '%s' for reload check", vp->fn);
            continue;
        }
       
        if(    check_stat.st_mtime != vp->db_stat.st_mtime
            || check_stat.st_ctime != vp->db_stat.st_ctime
            || check_stat.st_ino   != vp->db_stat.st_ino
            || check_stat.st_dev   != vp->db_stat.st_dev) {

            vnm_db_t* new_db = vnm_db_parse(vp->fn, &vp->db_stat);
            if(new_db) {
                vnm_db_t* old_db = vp->db;
                rcu_assign_pointer(vp->db, new_db);
                synchronize_rcu();
                vnm_db_destruct(old_db);
                VSL(SLT_CLI, 0, "vmod_netmapper: JSON database '%s' reloaded with new data", vp->fn); // CLI??
            }
            else {
                VSL(SLT_Error, 0, "vmod_netmapper: JSON database '%s' reload failed, continuing with old data", vp->fn);
            }
        }
    }

    return NULL;
}

static void per_vcl_fini(void* vp_asvoid) {
    vnm_priv_t* vp = vp_asvoid;

    // clean up the updater thread
    pthread_cancel(vp->updater);
    pthread_join(vp->updater, NULL);

    // free the most-recent data
    vnm_db_destruct(vp->db);
    free(vp->fn);
}

/*****************************
 * Actual VMOD/VCL/VRT Hooks *
 *****************************/

void vmod_init(struct sess *sp, struct vmod_priv *priv, const char* json_path, const int reload_interval) {
    if(priv->priv) {
        // I think this would be a bug, but it's remotely possible
        //   this is a situation that has to be handled manually
        //   during some reload by destructing ourselves, or that
        //   multiple threads can run vcl_init() for one VCL?
        WSP(sp, SLT_Error, "vmod_netmapper: per-VCL double-init! Bug?");
        abort();
    }

    vnm_priv_t* vp = malloc(sizeof(vnm_priv_t));
    vp->reload_check_interval = reload_interval;
    vp->fn = strdup(json_path);
    vp->db = vnm_db_parse(vp->fn, &vp->db_stat);
    if(!vp->db) {
        VSL(SLT_Error, 0, "vmod_netmapper: Failed initial load of JSON netmapper database %s", vp->fn);
        abort();
    }
    pthread_create(&vp->updater, NULL, updater_start, vp);
    priv->priv = vp;
    priv->free = per_vcl_fini;
}

// Crazy hack to get per-thread rcu register/unregister, even though
//   Varnish doesn't give us per-thread hooks for the workers
//   (at least, not that I noticed...)
// Note that it doesn't matter whether some threads are using two different
//   databases from different VCLs with different JSON files.  RCU thread
//   registration is just a per-thread global thing.
static pthread_key_t unreg_hack;
static pthread_once_t unreg_hack_once = PTHREAD_ONCE_INIT;
static void destruct_rcu(void* x) { pthread_setspecific(unreg_hack, NULL); rcu_unregister_thread(); }
static void make_unreg_hack(void) { pthread_key_create(&unreg_hack, destruct_rcu); }

const char* vmod_map(struct sess *sp, struct vmod_priv* priv, const char* ip_string) {
    assert(sp); assert(priv); assert(priv->priv); assert(ip_string);
    CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

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
    const vnm_priv_t* vp = priv->priv;
    const vnm_str_t* str = vnm_lookup(rcu_dereference(vp->db), ip_string);
    if(str)
        rv = vnm_str_to_vcl(sp, str);

    // normal rcu reader stuff
    rcu_read_unlock();
    rcu_thread_offline();

    return rv;
}
