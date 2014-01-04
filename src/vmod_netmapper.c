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

// note, the set of databases is indexed at runtime by a text
//  label, and we just iterate strcmp to look them up.  If anyone
//  actually has a lot of databases and cares, please submit a patch that
//  implements a hashtable for them!

typedef struct {
    unsigned reload_check_interval;
    char* label;
    char* fn;
    vnm_db_t* db;
    pthread_t updater;
    struct stat db_stat;
} vnm_db_file_t;

typedef struct {
    unsigned db_count;
    vnm_db_file_t** dbs;
} vnm_priv_t;

// Copy a str_t*'s data to a const char* in the session workspace,
//   so that after return we're not holding references to data in
//   the vnm db, so that it can be swapped for update between...
static const char* vnm_str_to_vcl(struct sess* sp, const vnm_str_t* str) {
    char* rv = NULL;
    if(str->data) {
        rv = WS_Alloc(sp->wrk->ws, str->len);
        if(!rv)
            WSP(sp, SLT_Error, "vmod_netmapper: no space for string retval!");
        else
            memcpy(rv, str->data, str->len);
    }
    return rv;
}

static void* updater_start(void* dbf_asvoid) {
    vnm_db_file_t* dbf = dbf_asvoid;
    struct stat check_stat;

    while(1) {
        sleep(dbf->reload_check_interval);
        if(stat(dbf->fn, &check_stat)) {
            VSL(SLT_Error, 0, "vmod_netmapper: Failed to stat JSON database '%s' for reload check", dbf->fn);
            continue;
        }
       
        if(    check_stat.st_mtime != dbf->db_stat.st_mtime
            || check_stat.st_ctime != dbf->db_stat.st_ctime
            || check_stat.st_ino   != dbf->db_stat.st_ino
            || check_stat.st_dev   != dbf->db_stat.st_dev) {

            vnm_db_t* new_db = vnm_db_parse(dbf->fn, &dbf->db_stat);
            if(new_db) {
                vnm_db_t* old_db = dbf->db;
                rcu_assign_pointer(dbf->db, new_db);
                synchronize_rcu();
                if(old_db)
                    vnm_db_destruct(old_db);
                VSL(SLT_CLI, 0, "vmod_netmapper: JSON database '%s' (re-)loaded with new data", dbf->fn); // CLI??
            }
            else {
                VSL(SLT_Error, 0, "vmod_netmapper: JSON database '%s' reload failed, continuing with old data", dbf->fn);
            }
        }
    }

    return NULL;
}

static void per_vcl_fini(void* vp_asvoid) {
    vnm_priv_t* vp = vp_asvoid;

    for(unsigned i = 0; i < vp->db_count; i++) {
        // clean up the updater thread
        pthread_cancel(vp->dbs[i]->updater);
        pthread_join(vp->dbs[i]->updater, NULL);

        // free the most-recent data
        vnm_db_destruct(vp->dbs[i]->db);
        free(vp->dbs[i]->fn);
        free(vp->dbs[i]->label);
        free(vp->dbs[i]);
    }

    free(vp->dbs);
    free(vp);
}

/*****************************
 * Actual VMOD/VCL/VRT Hooks *
 *****************************/

void vmod_init(struct sess *sp, struct vmod_priv *priv, const char* db_label, const char* json_path, const int reload_interval) {
    vnm_priv_t* vp = priv->priv;

    if(!vp) {
        priv->priv = vp = calloc(1, sizeof(vnm_priv_t));
        priv->free = per_vcl_fini;
    }

    const unsigned db_idx = vp->db_count++;
    vp->dbs = realloc(vp->dbs, vp->db_count * sizeof(vnm_db_file_t*));
    vnm_db_file_t* dbf = vp->dbs[db_idx] = malloc(sizeof(vnm_db_file_t));

    dbf->reload_check_interval = reload_interval;
    dbf->fn = strdup(json_path);
    dbf->label = strdup(db_label);
    memset(&dbf->db_stat, 0, sizeof(struct stat));
    dbf->db = vnm_db_parse(dbf->fn, &dbf->db_stat);
    if(!dbf->db)
        VSL(SLT_Error, 0, "vmod_netmapper: Failed initial load of JSON netmapper database %s (will keep trying periodically)", dbf->fn);

    pthread_create(&dbf->updater, NULL, updater_start, dbf);
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

const char* vmod_map(struct sess *sp, struct vmod_priv* priv, const char* db_label, const char* ip_string) {
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

    // static database index, no thread concerns during runtime...
    const vnm_priv_t* vp = priv->priv;
    const vnm_db_file_t* dbf = NULL;
    for(unsigned i = 0; i < vp->db_count; i++) {
        if(!strcmp(db_label, vp->dbs[i]->label)) {
            dbf = vp->dbs[i];
            break;
        }
    }

    const char* rv = NULL;

    if(!dbf) {
        VSL(SLT_Error, 0, "vmod_netmapper: JSON database label '%s' is not configured!", db_label);
    }
    else {
        // normal rcu reader stuff
        rcu_thread_online();
        rcu_read_lock();

        const vnm_db_t* dbptr = rcu_dereference(dbf->db);
        if(dbptr) {
            // search net database.  if match, convert
            //  string to a vcl string and return it...
            const vnm_str_t* str = vnm_lookup(dbptr, ip_string);
            if(str)
                rv = vnm_str_to_vcl(sp, str);
        }
        else {
            VSL(SLT_Error, 0, "vmod_netmapper: JSON database label '%s' was never succesfully loaded!", db_label);
        }

        // normal rcu reader stuff
        rcu_read_unlock();
        rcu_thread_offline();
    }

    return rv;
}
