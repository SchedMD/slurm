# Makefile for accounting_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

if HAVE_OPENSSL
gold_lib = accounting_storage_gold.la
else
gold_lib =
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = $(gold_lib)
if HAVE_OPENSSL
accounting_storage_gold_la_SOURCES = accounting_storage_gold.c
accounting_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

# Add libcommon to provide some symbols that are not
#  available in slurmctld (create_jobacct_job_rec)

accounting_storage_gold_la_LIBADD  = \
	$(top_builddir)/src/database/libslurm_gold.la	
accounting_storage_gold_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_gold.la

else
EXTRA_accounting_storage_gold_la_SOURCES = accounting_storage_gold.c
endif

