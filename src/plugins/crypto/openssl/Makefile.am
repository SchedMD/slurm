# Makefile for crypto/openssl plugin

AUTOMAKE_OPTIONS = foreign

if HAVE_OPENSSL
openssl_lib = crypto_openssl.la
else
openssl_lib =
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = $(openssl_lib)

if HAVE_OPENSSL
crypto_openssl_la_SOURCES =	\
	crypto_openssl.c

# Add libslurm to switch_elan to provide some symbols that are not
#  available in slurmctld (slurm_setenvpf)

crypto_openssl_la_LIBADD  = $(OPEN_LIBS) \
			$(top_builddir)/src/api/libslurm.la

crypto_openssl_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_crypto_openssl_la_SOURCES =	\
	crypto_openssl.c
endif
