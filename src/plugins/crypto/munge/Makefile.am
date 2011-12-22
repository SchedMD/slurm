# Makefile for crypto/munge plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

# Add your plugin to this line, following the naming conventions.
if BUILD_MUNGE

# Munge crypto plugin
pkglib_LTLIBRARIES = crypto_munge.la
crypto_munge_la_SOURCES = crypto_munge.c
crypto_munge_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
crypto_munge_la_LIBADD =  $(MUNGE_LIBS)

else
EXTRA_crypto_munge_la_SOURCES = crypto_munge.c
endif
