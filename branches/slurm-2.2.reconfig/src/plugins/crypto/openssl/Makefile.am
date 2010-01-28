# Makefile for crypto/openssl plugin

AUTOMAKE_OPTIONS = foreign

if HAVE_OPENSSL
openssl_lib = crypto_openssl.la
else
openssl_lib =
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = $(openssl_lib)

if HAVE_OPENSSL
crypto_openssl_la_SOURCES = crypto_openssl.c

crypto_openssl_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(SSL_LDFLAGS)
crypto_openssl_la_LIBADD   = $(SSL_LIBS)

else
EXTRA_crypto_openssl_la_SOURCES = crypto_openssl.c
endif
