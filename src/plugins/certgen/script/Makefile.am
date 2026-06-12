AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = certgen_script.la

certgen_script_la_SOURCES = certgen_script.c
certgen_script_la_LDFLAGS = $(PLUGIN_FLAGS)

REF = certgen.sh.txt keygen.sh.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
certgen_script_la_LIBADD = $(LIB_REF)
