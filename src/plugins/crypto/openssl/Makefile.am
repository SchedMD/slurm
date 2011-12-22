# Makefile for crypto/openssl plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS) -I$(top_srcdir)/src/common

if BUILD_OPENSSL
pkglib_LTLIBRARIES = crypto_openssl.la
crypto_openssl_la_SOURCES = crypto_openssl.c

crypto_openssl_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(SSL_LDFLAGS)
crypto_openssl_la_LIBADD   = $(SSL_LIBS)

else
EXTRA_crypto_openssl_la_SOURCES = crypto_openssl.c
endif
