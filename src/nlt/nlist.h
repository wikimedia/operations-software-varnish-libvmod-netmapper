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

#ifndef NLIST_H
#define NLIST_H

#include "config.h"
#include <inttypes.h>
#include <stdbool.h>
#include "ntree.h"

typedef struct _nlist nlist_t;

nlist_t* nlist_new(void);

void nlist_destroy(nlist_t* nl);

// true retval indicates network had bits beyond the mask,
//  which is *not* fatal, but should be warned about, because
//  those bits will be auto-cleared in practice, and probably indicates bad input
bool nlist_append(nlist_t* nl, const uint8_t* ipv6, const unsigned mask, const unsigned dclist);

// Call this when all nlist_append() are complete. 
void nlist_finish(nlist_t* nl);

// must pass through _finish() before xlate!
ntree_t* nlist_xlate_tree(const nlist_t* nl_a);

#endif // NLIST_H
