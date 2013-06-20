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
#include "nlist.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define NLIST_INITSIZE 64
#define MASK_DELETED 0xFFFFFFFF

typedef struct {
    uint8_t ipv6[16];
    unsigned mask;
    unsigned dclist;
} net_t;

struct _nlist {
    net_t* nets;
    unsigned alloc;
    unsigned count;
    bool finished;
};

nlist_t* nlist_new(void) {
    nlist_t* nl = malloc(sizeof(nlist_t));
    nl->nets = malloc(sizeof(net_t) * NLIST_INITSIZE);
    nl->alloc = NLIST_INITSIZE;
    nl->count = 0;
    nl->finished = false;
    return nl;
}

void nlist_destroy(nlist_t* nl) {
    assert(nl);
    free(nl->nets);
    free(nl);
}

static bool clear_mask_bits(uint8_t* ipv6, const unsigned mask) {
    assert(ipv6); assert(mask < 129);

    bool maskbad = false;

    if(mask) {
        const unsigned revmask = 128 - mask;
        const unsigned byte_mask = ~(0xFF << (revmask & 7)) & 0xFF;
        unsigned bbyte = 15 - (revmask >> 3);

        if(ipv6[bbyte] & byte_mask) {
            maskbad = true;
            ipv6[bbyte] &= ~byte_mask;
        }

        while(++bbyte < 16) {
            if(ipv6[bbyte]) {
                maskbad = true;
                ipv6[bbyte] = 0;
            }
        }
    }
    else if(memcmp(ipv6, &ip6_zero, 16)) {
        maskbad = true;
        memset(ipv6, 0, 16);
    }

    return maskbad;
}

// Sort an array of net_t.  Sort prefers
//   lowest network number, smallest mask.
static int net_sorter(const void* a_void, const void* b_void) {
    assert(a_void); assert(b_void);
    const net_t* a = (const net_t*)a_void;
    const net_t* b = (const net_t*)b_void;
    int rv = memcmp(a->ipv6, b->ipv6, 16);
    if(!rv)
        rv = a->mask - b->mask;
    return rv;
}

// sort and check for dupes, true retval indicates failure due to dupes
static bool nlist_normalize1(nlist_t* nl) {
    assert(nl);

    if(nl->count) {
        qsort(nl->nets, nl->count, sizeof(net_t), net_sorter);

        for(unsigned i = 0; i < (nl->count - 1); i++) {
            net_t* net_a = &nl->nets[i];
            net_t* net_b = &nl->nets[i + 1];
            if(net_a->mask == net_b->mask && !memcmp(net_a->ipv6, net_b->ipv6, 16)) {
               return true;
            }
        }
    }

    return false;
}

static bool masked_net_eq(const uint8_t* v6a, const uint8_t* v6b, const unsigned mask) {
    assert(v6a); assert(v6b);
    assert(mask < 128U); // 2x128 would call here w/ 127...

    const unsigned bytes = mask >> 3;
    assert(bytes < 16U);

    const unsigned bytemask = (0xFF00 >> (mask & 7)) & 0xFF;
    return !memcmp(v6a, v6b, bytes)
        && (v6a[bytes] & bytemask) == (v6b[bytes] & bytemask);
}

static bool mergeable_nets(const net_t* na, const net_t* nb) {
    assert(na); assert(nb);
    return (na->mask == nb->mask
        && na->dclist == nb->dclist
        && masked_net_eq(na->ipv6, nb->ipv6, na->mask - 1));
}

bool nlist_append(nlist_t* nl, const uint8_t* ipv6, const unsigned mask, const unsigned dclist) {
    assert(nl); assert(ipv6);

    if(nl->count == nl->alloc) {
        nl->alloc <<= 1U;
        nl->nets = realloc(nl->nets, sizeof(net_t) * nl->alloc);
    }
    net_t* this_net = &nl->nets[nl->count++];
    memcpy(this_net->ipv6, ipv6, 16U);
    this_net->mask = mask;
    this_net->dclist = dclist;

    // for raw input, just correct any netmask errors as we insert,
    //   as these will screw up later sorting for normalize1
    return clear_mask_bits(this_net->ipv6, mask);
}

// merge adjacent nets with identical dclists recursively...
static void nlist_normalize2(nlist_t* nl) {
    assert(nl);

    if(nl->count) {
        unsigned idx = nl->count;
        unsigned newcount = nl->count;
        while(--idx > 0) {
            net_t* nb = &nl->nets[idx];
            net_t* na = &nl->nets[idx - 1];
            if(mergeable_nets(na, nb)) {
                // na should have the differential bit clear
                //   thanks to pre-sorting, so just needs mask--
                nb->mask = MASK_DELETED;
                na->mask--;
                newcount--;
                unsigned upidx = idx + 1;
                while(upidx < nl->count) {
                    net_t* nc = &nl->nets[upidx];
                    if(nc->mask != MASK_DELETED) {
                        if(mergeable_nets(na, nc)) {
                            nc->mask = MASK_DELETED;
                            na->mask--;
                            newcount--;
                        }
                        else {
                            break;
                        }
                    }
                    upidx++;
                }
            }
        }

        if(newcount != nl->count) { // merges happened
            net_t* newnets = malloc(sizeof(net_t) * newcount);
            unsigned newidx = 0;
            for(unsigned i = 0; i < nl->count; i++) {
                net_t* n = &nl->nets[i];
                if(n->mask != MASK_DELETED)
                    memcpy(&newnets[newidx++], n, sizeof(net_t));
            }
            assert(newidx == newcount);
            free(nl->nets);
            nl->nets = newnets;
            nl->count = newcount;
            nl->alloc = newcount;
        }
        else { // just optimize nets size
            nl->alloc = nl->count;
            nl->nets = realloc(nl->nets, sizeof(net_t) * nl->alloc);
        }
    }
}

bool nlist_finish(nlist_t* nl) {
    assert(nl);

    bool rv = nlist_normalize1(nl);
    if(!rv) {
        nlist_normalize2(nl);
        nl->finished = true;
    }

    return rv;
}

static bool net_subnet_of(const net_t* sub, const net_t* super) {
    assert(sub); assert(super);
    assert(sub->mask < 129);
    assert(super->mask < 129);

    bool rv = false;
    if(sub->mask >= super->mask) {
        const unsigned wbyte = (super->mask >> 3);
        const unsigned byte_mask = (0xFF << (8 - (super->mask & 7))) & 0xFF;
        if(!memcmp(sub->ipv6, super->ipv6, wbyte)) {
            if(wbyte == 16 || (super->ipv6[wbyte] & byte_mask) == (sub->ipv6[wbyte] & byte_mask))
                rv = true;
        }
    }

    return rv;
}

static unsigned nxt_rec(const net_t** nl, const net_t* const nl_end, ntree_t* nt, net_t tree_net);

static void nxt_rec_dir(const net_t** nlp, const net_t* const nl_end, ntree_t* nt, net_t tree_net, const unsigned nt_idx, const bool direction) {
    assert(nlp); assert(nl_end); assert(nt);
    assert(tree_net.mask < 129 && tree_net.mask > 0);

    const net_t* nl = *nlp;
    unsigned cnode;

    // If items remain in the list, and the next list item
    //   is a subnet of (including exact match for) the current
    //   ntree node...
    if(nl < nl_end && net_subnet_of(nl, &tree_net)) {
        // exact match, consume...
        if(tree_net.mask == nl->mask && !memcmp(tree_net.ipv6, nl->ipv6, 16)) { // exact match on zero
            (*nlp)++; // consume *nlp and move to next
            // need to pre-check for a deeper subnet next in the list.
            // We use the consumed entry as the new default and keep recursing
            //   if deeper subnets exist.  If they don't, we assign and end recursion...
            const net_t* nl_next = *nlp;
            if(nl_next < nl_end && net_subnet_of(nl_next, nl)) {
                tree_net.dclist = nl->dclist;
                cnode = nxt_rec(nlp, nl_end, nt, tree_net);
            }
            else {
                cnode = NN_SET_DCLIST(nl->dclist);
            }
        }
        // Not an exact match, so just keep recursing towards such a match...
        else {
            cnode = nxt_rec(nlp, nl_end, nt, tree_net);
        }
    }
    // list item isn't a subnet of the current tree node, and due to our
    //   normalization that means there are no such list items remaining,
    //   so terminate the recursion with the current default dclist.
    else {
        cnode = NN_SET_DCLIST(tree_net.dclist);
    }

    // store direct or recursed result.  Note we have to wait until
    //   here to deref nt->store[nt_idx] because recursion could
    //   re-allocate nt->store[] during nxt_rec()'s ntree_add_node() call.
    if(direction)
        nt->store[nt_idx].one = cnode;
    else
        nt->store[nt_idx].zero = cnode;
}

static unsigned nxt_rec(const net_t** nl, const net_t* const nl_end, ntree_t* nt, net_t tree_net) {
    assert(nl); assert(nl_end); assert(nt);
    assert(tree_net.mask < 128);
    tree_net.mask++; // now mask for zero/one stubs

    const unsigned nt_idx = ntree_add_node(nt);
    nxt_rec_dir(nl, nl_end, nt, tree_net, nt_idx, false);
    SETBIT_v6(tree_net.ipv6, tree_net.mask - 1);
    nxt_rec_dir(nl, nl_end, nt, tree_net, nt_idx, true);

    // catch missed optimizations during final translation,
    //   which should be on the default dclist zero
    //   due to it being implicit for undefined nets in the list,
    //   and thus not merged with true list entries...
    unsigned rv;
    if((nt->store[nt_idx].zero == nt->store[nt_idx].one) && (nt_idx > 0)) {
        assert(nt->store[nt_idx].zero == NN_SET_DCLIST(0));
        nt->count--; // delete the just-added node
        rv = NN_SET_DCLIST(0); // retval is now a dclist rather than a node...
    }
    else {
        rv = nt_idx;
    }
    return rv;
}

ntree_t* nlist_xlate_tree(const nlist_t* nl) {
    assert(nl);
    assert(nl->finished);

    ntree_t* nt = ntree_new();
    const net_t* nlnet = &nl->nets[0];
    const net_t* const nlnet_end = &nl->nets[nl->count];
    net_t tree_net = {
        .ipv6 = { 0 },
        .mask = 0,
        .dclist = 0
    };

    // Special-case: if a list entry for ::/0 exists, it will
    //   be first in the list, and it needs to be skipped
    //   over (with its dclist as the new default) before
    //   recursing (because ::/0 is the first node of the
    //   tree itself).
    if(nl->count && !nl->nets[0].mask) {
        tree_net.dclist = nl->nets[0].dclist;
        nlnet++;
    }

    // recursively build the tree from the list
    nxt_rec(&nlnet, nlnet_end, nt, tree_net);

    // assert that the whole list was consumed
    assert(nlnet == nlnet_end);

    // finalize the tree
    ntree_finish(nt);

    return nt;
}

