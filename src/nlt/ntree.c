/* Copyright © 2012 Brandon L Black <blblack@gmail.com>
 *
 * This file is part of gdnsd-plugin-geoip.
 *
 * gdnsd-plugin-geoip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gdnsd-plugin-geoip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gdnsd.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "ntree.h"
#include <assert.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

// unaligned 32-bit access, copied from gdnsd's compiler.h
struct _gdnsd_una32 { uint32_t x; } __attribute__((__packed__));
#define gdnsd_get_una32(_p) (((const struct _gdnsd_una32*)(_p))->x)

// Initial node allocation count,
//   must be power of two due to alloc code,
static const unsigned NT_SIZE_INIT = 128;

ntree_t* ntree_new(void) {
    ntree_t* newtree = malloc(sizeof(ntree_t));
    newtree->store = malloc(NT_SIZE_INIT * sizeof(nnode_t));
    newtree->count = 0;
    newtree->alloc = NT_SIZE_INIT; // set to zero on fixation
    return newtree;
}

void ntree_destroy(ntree_t* tree) {
    assert(tree);
    free(tree->store);
    free(tree);
}

unsigned ntree_add_node(ntree_t* tree) {
    assert(tree);
    assert(tree->alloc);
    if(tree->count == tree->alloc) {
        tree->alloc <<= 1;
        tree->store = realloc(tree->store, tree->alloc * sizeof(nnode_t));
    }
    const unsigned rv = tree->count;
    assert(rv < (1U << 24));
    tree->count++;
    return rv;
}

// returns either a node offset for the true ipv4 root
//   node at exactly ::/96, or a terminal dclist
//   for a wholly enclosing supernet.  This is cached
//   for the tree to make various ipv4-related lookups
//   faster and simpler.
static unsigned ntree_find_v4root(const ntree_t* tree) {
    assert(tree);

    unsigned offset = 0;
    unsigned mask_depth = 96;
    do {
        assert(offset < tree->count);
        offset = tree->store[offset].zero;
    } while(--mask_depth && !NN_IS_DCLIST(offset));

    return offset;
}

void ntree_finish(ntree_t* tree) {
    assert(tree);
    tree->alloc = 0; // flag fixed, will fail asserts on add_node, etc now
    tree->store = realloc(tree->store, tree->count * sizeof(nnode_t));
    tree->ipv4 = ntree_find_v4root(tree);
}

static inline bool CHKBIT_v6(const uint8_t* ipv6, const unsigned bit) {
    assert(ipv6);
    assert(bit < 128);
    return ipv6[bit >> 3] & (1UL << (~bit & 7));
}

static unsigned ntree_lookup_v6(const ntree_t* tree, const uint8_t* ip) {
    assert(tree); assert(ip);

    unsigned chkbit = 0;
    unsigned offset = 0;
    do {
        assert(offset < tree->count);
        const nnode_t* current = &tree->store[offset];
        assert(current->one && current->zero);
        offset = CHKBIT_v6(ip, chkbit++) ? current->one : current->zero;
        assert(chkbit < 129);
    } while(!NN_IS_DCLIST(offset));

    assert(offset != NN_UNDEF); // the special v4-like undefined areas
    return NN_GET_DCLIST(offset);
}

static inline bool CHKBIT_v4(const uint32_t ip, const unsigned maskbit) {
    assert(maskbit < 32U);
    return ip & (1U << (31U - maskbit));
}

static unsigned ntree_lookup_v4(const ntree_t* tree, const uint32_t ip) {
    assert(tree); assert(ip);
    assert(tree->ipv4);

    unsigned chkbit = 0;
    unsigned offset = tree->ipv4;
    while(!NN_IS_DCLIST(offset)) {
        assert(offset < tree->count);
        const nnode_t* current = &tree->store[offset];
        assert(current->one && current->zero);
        offset = CHKBIT_v4(ip, chkbit++) ? current->one : current->zero;
        assert(chkbit < 33);
    }

    assert(offset != NN_UNDEF); // the special v4-like undefined areas
    return NN_GET_DCLIST(offset);
}

// if "addr" is in any v4-compatible spaces other than
//   v4compat (our canonical one), convert to v4compat.
// returns address zero if no conversion
static uint32_t v6_v4fixup(const uint8_t* in) {
    assert(in);

    uint32_t ip_out = 0;

    if(!memcmp(in, start_v4mapped, 12) || !memcmp(in, start_siit, 12))
        ip_out = ntohl(gdnsd_get_una32(&in[12]));
    else if(!memcmp(in, start_teredo, 4))
        ip_out = ntohl(gdnsd_get_una32(&in[12]) ^ 0xFFFFFFFF);
    else if(!memcmp(in, start_6to4, 2))
        ip_out = ntohl(gdnsd_get_una32(&in[2]));

    return ip_out;
}

unsigned ntree_lookup(const ntree_t* tree, const struct sockaddr* sa) {
    assert(tree); assert(sa);
    assert(!tree->alloc); // ntree_finish() was called
    assert(tree->ipv4); // must be a non-zero node offset or a dclist w/ high-bit set

    unsigned rv;

    if(sa->sa_family == AF_INET) {
        const struct sockaddr_in* sin = (const struct sockaddr_in*)sa;
        rv = ntree_lookup_v4(tree, ntohl(sin->sin_addr.s_addr));
    }
    else {
        assert(sa->sa_family == AF_INET6);
        const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)sa;
        const uint32_t ipv4 = v6_v4fixup(sin6->sin6_addr.s6_addr);
        if(ipv4) {
            rv = ntree_lookup_v4(tree, ipv4);
        }
        else {
            rv = ntree_lookup_v6(tree, sin6->sin6_addr.s6_addr);
        }
    }

    return rv;
}

