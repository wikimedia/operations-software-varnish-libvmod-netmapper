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

typedef struct {
    uint8_t ipv6[16];
    unsigned mask;
    unsigned dclist;
} net_t;

struct _nlist {
    net_t* nets;
    unsigned alloc;
    unsigned count;
    bool normalized;
};

nlist_t* nlist_new(void) {
    nlist_t* nl = malloc(sizeof(nlist_t));
    nl->nets = malloc(sizeof(net_t) * NLIST_INITSIZE);
    nl->alloc = NLIST_INITSIZE;
    nl->count = 0;
    nl->normalized = false;
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
    bool rv = false;
    if(na->dclist == nb->dclist) {
        if(na->mask == nb->mask)
            rv = masked_net_eq(na->ipv6, nb->ipv6, na->mask - 1);
        else if(na->mask < nb->mask)
            rv = masked_net_eq(na->ipv6, nb->ipv6, na->mask);
    }
    return rv;
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

    return clear_mask_bits(this_net->ipv6, mask);
}

static bool net_eq(const net_t* na, const net_t* nb) {
    assert(na); assert(nb);
    return na->mask == nb->mask && !memcmp(na->ipv6, nb->ipv6, 16);
}

// do a single pass of forward-normalization
//   on a sorted nlist, then sort the result.
static bool nlist_normalize_1pass(nlist_t* nl) {
    assert(nl); assert(nl->count);

    bool rv = false;

    const unsigned oldcount = nl->count;
    unsigned newcount = nl->count;
    unsigned i = 0;
    while(i < oldcount) {
        net_t* na = &nl->nets[i];
        unsigned j = i + 1;
        while(j < oldcount) {
            net_t* nb = &nl->nets[j];
            if(net_eq(na, nb)) { // net+mask match, dclist may or may not match
                // fall-through past else - ugly, but easier for future upstream merges
            }
            else if(mergeable_nets(na, nb)) { // dclists match, nets adjacent (masks equal) or subnet-of
                if(na->mask == nb->mask)
                    na->mask--;
            }
            else {
                break;
            }
            nb->mask = 0xFFFF; // illegally-huge, to sort deletes later
            memset(nb->ipv6, 0xFF, 16); // all-1's, also for sort...
            newcount--;
            j++;
        }
        i = j;
    }

    if(newcount != oldcount) { // merges happened above
        // the "deleted" entries have all-1's IPs and >legal masks, so they
        //   sort to the end...
        qsort(nl->nets, oldcount, sizeof(net_t), net_sorter);

        // reset the count to ignore the deleted entries at the end
        nl->count = newcount;

        // signal need for another pass
        rv = true;
    }

    return rv;
}

static void nlist_normalize(nlist_t* nl, const bool post_merge) {
    assert(nl);

    if(nl->count) {
        // initial sort, unless already sorted by the merge process
        if(!post_merge)
            qsort(nl->nets, nl->count, sizeof(net_t), net_sorter);

        // iterate merge+sort passes until no further merges are found
        while(nlist_normalize_1pass(nl))
            ; // empty

        // optimize storage space
        if(nl->count != nl->alloc) {
            assert(nl->count < nl->alloc);
            nl->alloc = nl->count;
            nl->nets = realloc(nl->nets, nl->alloc * sizeof(net_t));
        }
    }

    nl->normalized = true;
}

void nlist_finish(nlist_t* nl) {
    assert(nl);
    if(!nl->normalized)
        nlist_normalize(nl, false);
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
        if(tree_net.mask == nl->mask) {
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

    unsigned rv = nt_idx;

    // catch missed optimizations during final translation
    if(nt->store[nt_idx].zero == nt->store[nt_idx].one && nt_idx > 0) {
        nt->count--; // delete the just-added node
        rv = nt->store[nt_idx].zero;
    }

    return rv;
}

ntree_t* nlist_xlate_tree(const nlist_t* nl) {
    assert(nl);
    assert(nl->normalized);

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
