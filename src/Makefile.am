AM_CPPFLAGS = -I$(VARNISHSRC)/include -I$(VARNISHSRC) -I$(srcdir)/nlt

vmoddir = $(libdir)/varnish/vmods
vmod_LTLIBRARIES = libvmod_netmapper.la

libvmod_netmapper_la_LDFLAGS = -module -export-dynamic -avoid-version -shared
libvmod_netmapper_la_LIBS = -lurcu-qsbr -ljansson
libvmod_netmapper_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_netmapper.c \
	vnm.c \
	vnm.h \
	vnm_strdb.c \
	vnm_strdb.h \
	nlt/nlist.c \
	nlt/nlist.h \
	nlt/ntree.c \
	nlt/ntree.h

vcc_if.c vcc_if.h: $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_netmapper.vcc
	@PYTHON@ $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_netmapper.vcc

VMOD_TDATA = tests/*.json
VMOD_TESTS = tests/*.vtc
.PHONY: $(VMOD_TESTS) $(VMOD_TDATA)

tests/*.vtc: libvmod_netmapper.la
	$(VARNISHTEST) -Dvarnishd=$(VARNISHD) -Dvmod_topbuild=$(abs_top_builddir) -Dvmod_topsrc=$(abs_top_srcdir) $(srcdir)/$@

check: $(VMOD_TESTS)

EXTRA_DIST = vmod_netmapper.vcc $(VMOD_TESTS) $(VMOD_TDATA)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
