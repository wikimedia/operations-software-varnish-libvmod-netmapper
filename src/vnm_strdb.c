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

#include "config.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "vnm_strdb.h"

struct _vnm_strdb {
    vnm_str_t* strings;
    unsigned count;
    unsigned alloc;
};

vnm_strdb_t* vnm_strdb_new(void) {
    vnm_strdb_t* d = malloc(sizeof(vnm_strdb_t));
    d->alloc = 8;
    d->count = 1;
    d->strings = malloc(d->alloc * sizeof(vnm_str_t));
    // note index zero is reserved as the no-match case with a NULL zero-len string...
    d->strings[0].data = NULL;
    d->strings[0].len = 0;
    return d;
}

unsigned vnm_strdb_add(vnm_strdb_t* d, const char* str) {
    assert(d); assert(str);

    if(d->count == d->alloc) {
        d->alloc <<= 1;
        d->strings = realloc(d->strings, d->alloc * sizeof(vnm_str_t));
    }

    const unsigned rv = d->count++;
    vnm_str_t* s = &d->strings[rv];
    s->len = strlen(str) + 1;
    s->data = malloc(s->len);
    memcpy(s->data, str, s->len);

    return rv;
}

const vnm_str_t* vnm_strdb_get(const vnm_strdb_t* d, const unsigned idx) {
    assert(d); assert(idx < d->count);
    return &d->strings[idx];
}

void vnm_strdb_destroy(vnm_strdb_t* d) {
    assert(d);
    for(unsigned i = 0; i < d->count; i++)
        free(d->strings[i].data);
    free(d->strings);
    free(d);
}

