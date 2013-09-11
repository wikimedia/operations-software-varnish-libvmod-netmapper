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

#ifdef NO_VARNISH
#define ERR(fmt,...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
#include "bin/varnishd/cache.h"
#define ERR(fmt,...) VSL(SLT_Error, 0, "vmod_netmapper: " fmt, ##__VA_ARGS__)
#endif

#include "vnm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <jansson.h>

#include "vnm_strdb.h"
#include "ntree.h"
#include "nlist.h"

struct _vnm_db_struct {
    ntree_t* tree;
    vnm_strdb_t* strdb;
};

void vnm_db_destruct(vnm_db_t* d) {
    ntree_destroy(d->tree);
    vnm_strdb_destroy(d->strdb);
    free(d);
}

static bool v6_subnet_of(const uint8_t* check, const unsigned check_mask, const uint8_t* v4, const unsigned v4_mask) {
    assert(check); assert(v4);
    assert(!(v4_mask & 7)); // all v4_mask are whole byte masks

    bool rv = false;

    if(check_mask >= v4_mask)
        rv = !memcmp(check, v4, (v4_mask >> 3));

    return rv;
}

static bool check_v4_issues(const uint8_t* ipv6, const unsigned mask) {
    assert(ipv6); assert(mask < 129);

    return (
          v6_subnet_of(ipv6, mask, start_v4mapped, 96)
       || v6_subnet_of(ipv6, mask, start_siit, 96)
       || v6_subnet_of(ipv6, mask, start_teredo, 32)
       || v6_subnet_of(ipv6, mask, start_6to4, 16)
    );
}

static bool append_string_to_nlist(const char* fn, const char* key, nlist_t* nl, const char* addr_mask, const unsigned stridx) {

    // convert "192.0.2.0/24\0" -> "192.0.2.0\0" + "24\0" in stack
    const unsigned inlen = strlen(addr_mask);
    char net_str[inlen + 1];
    memcpy(net_str, addr_mask, inlen + 1);
    char* mask_str = strchr(net_str, '/');
    if(!mask_str) {
        ERR("JSON database '%s', key '%s': '%s' does not parse as addr/mask", fn, key, net_str);
        return true;
    }
    *mask_str++ = '\0';

    // translate text address + mask to sockaddr stuff (putting mask in port field)
    struct addrinfo* ainfo = NULL;
    const struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV,
        .ai_family = AF_UNSPEC,
        .ai_socktype = 0,
        .ai_protocol = 0,
        .ai_addrlen = 0,
        .ai_addr = NULL,
        .ai_canonname = NULL,
        .ai_next = NULL
    };
    const int addr_err = getaddrinfo(net_str, mask_str, &hints, &ainfo);
    if(addr_err) {
        ERR("JSON database '%s', key '%s': '%s' does not parse as addr/mask: %s", fn, key, net_str, gai_strerror(addr_err));
        return true;
    }

    // Copy data to simple ipv6 + mask values, check for errors
 
    unsigned mask;
    uint8_t ipv6[16];

    if(ainfo->ai_family == AF_INET6) {
        const struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ainfo->ai_addr;
        mask = ntohs(sin6->sin6_port);
        memcpy(ipv6, sin6->sin6_addr.s6_addr, 16);
        if(check_v4_issues(ipv6, mask)) {
            ERR("JSON database '%s', key '%s': '%s' covers illegal IPv4-like space", fn, key, addr_mask);
            freeaddrinfo(ainfo);
            return true;
        }
    }
    else {
        assert(ainfo->ai_family == AF_INET);
        const struct sockaddr_in* sin = (struct sockaddr_in*)ainfo->ai_addr;
        mask = ntohs(sin->sin_port) + 96;
        memset(ipv6, 0, 16);
        memcpy(&ipv6[12], &sin->sin_addr.s_addr, 4);
    }

    if(mask > 128) {
        ERR("JSON database '%s', key '%s': '%s' has illegal netmask", fn, key, addr_mask);
        freeaddrinfo(ainfo);
        return true;
    }

    freeaddrinfo(ainfo);

    // actually stick data in the nlist using existing call
    if(nlist_append(nl, ipv6, mask, stridx))
        ERR("JSON database '%s', key '%s': '%s' has bits beyond the network mask, which were auto-cleared!", fn, key, addr_mask);

    return false;
}

vnm_db_t* vnm_db_parse(const char* fn, struct stat* db_stat) {
    assert(fn);

    struct stat db_stat_precheck;
    if(stat(fn, &db_stat_precheck)) {
        ERR("Failed to stat() JSON database %s: %u", fn, errno);
        return NULL;
    }

    json_error_t errobj;
    json_t* toplevel = json_load_file(fn, 0, &errobj);

    if(!toplevel) {
        ERR("Failed to load JSON database %s: %s", fn, errobj.text);
        return NULL;
    }

    if(!json_is_object(toplevel)) {
        ERR("JSON database %s top-level is not an object!", fn);
        json_decref(toplevel);
        return NULL;
    }

    struct stat db_stat_postcheck;
    if(stat(fn, &db_stat_postcheck)) {
        ERR("Failed to stat() JSON database %s: %u", fn, errno);
        json_decref(toplevel);
        return NULL;
    }

    if(    db_stat_postcheck.st_mtime != db_stat_precheck.st_mtime
        || db_stat_postcheck.st_ctime != db_stat_precheck.st_ctime
        || db_stat_postcheck.st_ino   != db_stat_precheck.st_ino
        || db_stat_postcheck.st_dev   != db_stat_precheck.st_dev) {
        ERR("JSON database %s changed while reading!", fn);
        json_decref(toplevel);
        return NULL;
    }

    nlist_t* templist = nlist_new();
    vnm_db_t* d = malloc(sizeof(vnm_db_t));
    d->tree = NULL;
    d->strdb = vnm_strdb_new();

    // iterate the keys...
    const char* key;
    json_t* val;
    void *iter = json_object_iter(toplevel);
    while(iter) {
        key = json_object_iter_key(iter);
        val = json_object_iter_value(iter);
        if(!json_is_array(val)) {
            ERR("JSON database %s: value for key '%s' should be an array!", fn, key);
            nlist_destroy(templist);
            vnm_strdb_destroy(d->strdb);
            free(d);
            json_decref(toplevel);
            return NULL;
        }

        const unsigned stridx = vnm_strdb_add(d->strdb, key);
        const unsigned nnets = json_array_size(val);
        for(unsigned i = 0; i < nnets; i++) {
            const json_t* net = json_array_get(val, i);
            const bool net_isstr = json_is_string(net);
            if(!net_isstr)
                ERR("JSON database %s: array member %u for key '%s' should be an address string!", fn, i, key);
            if(!net_isstr || append_string_to_nlist(fn, key, templist, json_string_value(net), stridx)) {
                nlist_destroy(templist);
                vnm_strdb_destroy(d->strdb);
                free(d);
                json_decref(toplevel);
                return NULL;
            }
        }
        iter = json_object_iter_next(toplevel, iter);
    }

    // add undefined areas for the translated v4 subspaces and optimize the list
    nlist_append(templist, start_v4mapped, 96, NN_UNDEF);
    nlist_append(templist, start_siit, 96, NN_UNDEF);
    nlist_append(templist, start_6to4, 16, NN_UNDEF);
    nlist_append(templist, start_teredo, 32, NN_UNDEF);
    if(nlist_finish(templist)) {
        ERR("JSON contains duplicate networks!");
        vnm_strdb_destroy(d->strdb);
        free(d);
        nlist_destroy(templist);
        json_decref(toplevel);
        return NULL;
    }

    // translate to tree for lookup
    d->tree = nlist_xlate_tree(templist);

    // free up temporary stuff
    nlist_destroy(templist);
    json_decref(toplevel);

    // copy out stat data for future checks
    if(db_stat)
        memcpy(db_stat, &db_stat_postcheck, sizeof(struct stat));

    return d;
}

const vnm_str_t* vnm_lookup(const vnm_db_t* d, const char* ip_string) {
    assert(d); assert(d->tree); assert(d->strdb); assert(ip_string);

    unsigned stridx = 0; // default, no-match

    // translate text address -> sockaddr
    struct addrinfo* ainfo = NULL;
    const struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST,
        .ai_family = AF_UNSPEC,
        .ai_socktype = 0,
        .ai_protocol = 0,
        .ai_addrlen = 0,
        .ai_addr = NULL,
        .ai_canonname = NULL,
        .ai_next = NULL
    };
    const int addr_err = getaddrinfo(ip_string, NULL, &hints, &ainfo);
    if(addr_err) {
        ERR("Client IP '%s' does not parse: %s", ip_string, gai_strerror(addr_err));
    }
    else {
        stridx = ntree_lookup(d->tree, ainfo->ai_addr);
    }

    return vnm_strdb_get(d->strdb, stridx);
}
