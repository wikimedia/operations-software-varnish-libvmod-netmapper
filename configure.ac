AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2013-2017 Brandon Black <bblack@wikimedia.org>])
AC_INIT([libvmod-netmapper],[1.5],[bblack@wikimedia.org],[libvmod-netmapper],[https://git.wikimedia.org/summary/operations%2Fsoftware%2Fvarnish%2Flibvmod-netmapper])
AC_CONFIG_MACRO_DIR([m4])
m4_ifndef([VARNISH_VMOD_INCLUDES], AC_MSG_ERROR([Need varnish.m4]))
AC_CONFIG_AUX_DIR([acaux])
AC_CONFIG_SRCDIR(src/vmod_netmapper.vcc)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE([dist-xz no-dist-gzip foreign tar-ustar -Wall])

AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CC_STDC
if test "x$ac_cv_prog_cc_c99" = xno; then
	AC_MSG_ERROR([Could not find a C99 compatible compiler])
fi
AC_PROG_CPP
AM_PROG_CC_C_O

AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET

# Check for rst utilities
AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], "no")
if test "x$RST2MAN" = "xno"; then
	AC_MSG_WARN([rst2man not found - not building man pages])
fi
AM_CONDITIONAL(HAVE_RST2MAN, [test "x$RST2MAN" != "xno"])

# Check for pkg-config
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/stdlib.h])

# Check for python
AC_CHECK_PROGS(PYTHON, [python3 python3.1 python3.2 python2.7 python2.6 python2.5 python2 python], [AC_MSG_ERROR([Python is needed to build this vmod, please install python.])])

PKG_CHECK_MODULES([libvarnishapi], [varnishapi])
PKG_CHECK_VAR([LIBVARNISHAPI_PREFIX], [varnishapi], [prefix])
PKG_CHECK_VAR([LIBVARNISHAPI_DATAROOTDIR], [varnishapi], [datarootdir])
PKG_CHECK_VAR([LIBVARNISHAPI_BINDIR], [varnishapi], [bindir])
PKG_CHECK_VAR([LIBVARNISHAPI_SBINDIR], [varnishapi], [sbindir])
AC_SUBST([LIBVARNISHAPI_DATAROOTDIR])

# Varnish include files tree
VARNISH_VMOD_INCLUDES
VARNISH_VMOD_DIR
VARNISH_VMODTOOL
# AC_PREFIX_DEFAULT acts too early in the configure run,
# so we can't use it
ac_default_prefix=$LIBVARNISHAPI_PREFIX

AC_PATH_PROG([VARNISHTEST], [varnishtest], [],
    [$LIBVARNISHAPI_BINDIR:$LIBVARNISHAPI_SBINDIR:$PATH])
AC_PATH_PROG([VARNISHD], [varnishd], [],
    [$LIBVARNISHAPI_SBINDIR:$LIBVARNISHAPI_BINDIR:$PATH])

AC_CHECK_FILE([${VAPI_INCLUDE_DIR}/vmod_abi.h],
    [
    AC_MSG_CHECKING([varnish ABI version])
    AC_SUBST([VMOD_ABI_VERSION],
         [`cat "${VAPI_INCLUDE_DIR}/vmod_abi.h" | grep "#define" | cut -d ' ' -f 4`])
    AC_MSG_RESULT([${VMOD_ABI_VERSION}])
    ], [
    AC_MSG_ERROR([Could not find vmod_abi.h. Need Varnish4 or higher])
    ])

XLIBS=$LIBS

# userspace-rcu for lockless netmap reload
AC_CHECK_HEADER(urcu-qsbr.h,[
     AC_CHECK_LIB([urcu-qsbr],[perror],[],AC_MSG_ERROR("liburcu-qsbr missing!"))
], AC_MSG_ERROR("urcu-qsbr.h missing!"))

# JSON parser for the input data
AC_CHECK_HEADER(jansson.h,[
     AC_CHECK_LIB([jansson],[json_object_update],[],AC_MSG_ERROR("libjansson missing!"))
], AC_MSG_ERROR("jansson.h missing!"))

LIBS=$XLIBS

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
