VARNISHSRC ?= $(top_srcdir)

AM_CPPFLAGS = -I$(VARNISHSRC)/include -I$(VARNISHSRC) -I$(srcdir)/nlt

COMMON_SRC = \
	vnm.c \
	vnm.h \
	vnm_strdb.c \
	vnm_strdb.h \
	nlt/nlist.c \
	nlt/nlist.h \
	nlt/ntree.c \
	nlt/ntree.h

vcc_if.c vcc_if.h: $(VARNISHSRC)/lib/libvmod_std/vmod.py $(srcdir)/vmod_netmapper.vcc
	@PYTHON@ $(VARNISHSRC)/lib/libvmod_std/vmod.py $(srcdir)/vmod_netmapper.vcc

vmoddir = $(libdir)/varnish/vmods
vmod_LTLIBRARIES = libvmod_netmapper.la

libvmod_netmapper_la_LDFLAGS = -module -export-dynamic -avoid-version -shared
libvmod_netmapper_la_LIBADD = -lurcu-qsbr -ljansson
libvmod_netmapper_la_SOURCES = vcc_if.c vcc_if.h vmod_netmapper.c $(COMMON_SRC)

bin_PROGRAMS = vnm_validate
vnm_validate_CPPFLAGS = $(AM_CPPFLAGS) -DNO_VARNISH
vnm_validate_LDADD = -ljansson
vnm_validate_SOURCES = vnm_validate.c $(COMMON_SRC)

VMOD_TDATA = tests/test01a.json tests/test01b.json tests/test01c.json
VMOD_TESTS = tests/test01.vtc
.PHONY: $(VMOD_TESTS) $(VMOD_TDATA)

$(VMOD_TESTS): libvmod_netmapper.la
	$(VARNISHTEST) -Dvarnishd=$(VARNISHD) -Dvmod_topbuild=$(abs_top_builddir) -Dvmod_topsrc=$(abs_top_srcdir) $(srcdir)/$@

check: $(VMOD_TESTS)

EXTRA_DIST = nlt/README vmod_netmapper.vcc $(VMOD_TESTS) $(VMOD_TDATA)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
