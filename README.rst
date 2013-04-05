=================
libvmod_netmapper
=================

XXX this project was recently copied from vmod_example.  Things like docs
are not at all up to date with the experimental code!

-------------------------------------------------
Varnish module to map an IP address to a string
-------------------------------------------------

:Author: Martin Blix Grydeland
:Date: 2011-05-26
:Version: 1.0
:Manual section: 3

SYNOPSIS
========

import netmapper;

DESCRIPTION
===========

Example Varnish vmod demonstrating how to write an out-of-tree Varnish vmod
for Varnish 3.0 and later.

Implements the traditional Hello World as a vmod.

FUNCTIONS
=========

hello
-----

Prototype
        ::

                hello(STRING S)
Return value
	STRING
Description
	Returns "Hello, " prepended to S
Example
        ::

                set resp.http.hello = netmapper.hello("World");

INSTALLATION
============

This is an netmapper skeleton for developing out-of-tree Varnish
vmods available from the 3.0 release. It implements the "Hello, World!" 
as a vmod callback. Not particularly useful in good hello world 
tradition,but demonstrates how to get the glue around a vmod working.

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

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

In your VCL you could then use this vmod along the following lines::
        
        import netmapper;

        sub vcl_deliver {
                # This sets resp.http.hello to "Hello, World"
                set resp.http.hello = netmapper.hello("World");
        }

HISTORY
=======

This manual page was released as part of the libvmod-netmapper package,
demonstrating how to create an out-of-tree Varnish vmod.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-netmapper project. See LICENSE for details.

* Copyright (c) 2011 Varnish Software
