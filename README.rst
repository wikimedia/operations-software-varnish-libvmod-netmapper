=================
libvmod_netmapper
=================

-------------------------------------------------
Varnish module to map an IP address to a string
-------------------------------------------------

:Author: Brandon Black
:Date: 2013-06-24
:Version: 1.0
:Manual section: 3

SYNOPSIS
========

::

    import netmapper;
    
    sub vcl_init {
        netmapper.init("/path/to/netmap.json", 42);
    }
    
    sub vcl_recv {
        set req.http.X-Foo = netmapper.map("" + client.ip);
    }

DESCRIPTION
===========

This module loads a JSON-formatted database which maps sets of IPv[46]
networks to unique strings, and then provides an interface for VCL code
to map IP addresses to those strings.

FUNCTIONS
=========

init
-----

Prototype
    ``init(STRING DatabaseFile, INT CheckInterval)``
Return value
    VOID
Description
    Initializes netmapper with a given JSON database and reload check
    interval (in seconds), for this VCL.  The database is checked via
    stat(2) for changes every check interval, and reloaded on the fly
    when altered.
Example
        ::

                sub vcl_init {
                    netmapper.init("/path/to/netmap.json", 42);
                }


map
-----

Prototype
    ``map(STRING IPAddr)``
Return value
    String, could be undefined if no match.
Description
    Maps the passed client IP address (in string form) against the
    data loaded from the JSON database, returning the string key
    of the set this address belongs to, or NULL (undefined) if
    not matched by any set.
Example
        ::

                sub vcl_recv {
                    set req.http.X-Foo = netmapper.map("" + client.ip);
                }


THE DATA
========

The JSON database takes the following form:

::

        {
            "localnets": ["127.0.0.0/8", "::1/128"],
            "Foo": [
                "192.0.2.0/24",
                "10.0.0.0/8",
                "2001:db8:1234::/48"
            ],
            "Bar": [
                "192.0.2.128/25",
                "172.16.0.0/12",
                "2001:db8:4231::/48"
            ]
        }

The module compiles this data into a binary tree for matching individual
IP addresses against the dataset and returning the associated key.  For
example, with the above dataset mapping "192.0.2.1" would return "Foo".

In the case that one network is a subnet of another, this will be
semi-gracefully handled and the subnet will return its key while the rest
of the supernet will return its own key.  If an address seems to
have a short mask (meaning, there are 1-bits in the host portion of the
address, according to the provided netmask), the host portion will be
cleared to match the provided mask.  Both of these conditions will
log warnings.

If the dataset contains exact duplicate networks or fails basic
parsing, the file will fail to load with an error.  This is fatal on
startup.  On reload attempts the existing dataset will continue
to be used until a new file is successfully reloaded.

IPv4-Compatible IPv6 Addresses
==============================

This module knows of five different relatively-trivial ways to map IPv4
addresses into the IPv6 address space.  These are shown below, with
C<NNNN:NNNN> in place of the copied IPv4 address bytes:

::

   ::NNNN:NNNN/96        # v4compat - canonical form for this module
   ::FFFF:NNNN:NNNN/96   # v4mapped
   ::FFFF:0:NNNN:NNNN/96 # SIIT
   2001::NNNN:NNNN/32    # Teredo (NNNN:NNNN is xor'd with FFFF:FFFF)
   2002:NNNN:NNNN::/16   # 6to4

The module's internal memory database is an IPv6 database, and any
IPv4 networks from the JSON input are stored in the v4compat space within
the internal IPv6 database.

When doing runtime lookups all other v4-like addresses (raw
IPv4 addresses, v4mapped, SIIT, Teredo, and 6to4) are converted to the
canonical v4compat IPv6 representation before querying the internal
database.  It is not legal to directly specify the other IPv4
subspaces (v4mapped, SIIT, Teredo, 6to4) in the JSON file directly,
or any subnet of those spaces (these will cause a db load failure,
much like duplicate networks above).

In practice, this means a network of "192.0.2.0/24" in the JSON file
will match for any of these other representations if the traffic happens
to arrive via some IPv4-to-IPv6 translation scheme.  The tradeoff is
you can't decide to map, for example, the 6to4 representation of a given
IPv4 network differently than the Teredo representation of the same
network.

INSTALLATION
============

Usage::

 ./configure VARNISHSRC=DIR [VMODDIR=DIR]

`VARNISHSRC` is the directory of the Varnish source tree for which to
compile your vmod. Both the `VARNISHSRC` and `VARNISHSRC/include`
will be added to the include search paths for your module.

Optionally you can also set the vmod install directory by adding
`VMODDIR=DIR` (defaults to the pkg-config discovered directory from your
Varnish installation).

Make targets:

* make - builds the vmod
* make install - installs your vmod in `VMODDIR`
* make check - runs the unit tests in ``src/tests/*.vtc``

HISTORY
=======

This manual page was released as part of the libvmod-netmapper package.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-netmapper project. See COPYING for details.

* Copyright (c) 2013 Brandon Black <bblack@wikimedia.org>
