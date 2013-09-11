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

#include "vnm.h"

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    if(argc != 2) {
        fprintf(stderr,"Must specify an input file!\n");
        return 99;
    }
    vnm_db_t* vdb = vnm_db_parse(argv[1], NULL);
    if(!vdb) {
        fprintf(stderr,"Parsing '%s' failed!\n", argv[1]);
        return 98;
    }
    vnm_db_destruct(vdb);
    fprintf(stderr,"OK\n");
    return 0;
}
