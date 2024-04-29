AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(S2N_CPPFLAGS)

pkglib_LTLIBRARIES = tls_s2n.la

tls_s2n_la_SOURCES = tls_s2n.c
tls_s2n_la_LDFLAGS = $(PLUGIN_FLAGS) $(S2N_LDFLAGS)
tls_s2n_la_LIBADD = $(S2N_LIBS)
#tls_s2n_la_CPPFLAGS = $(S2N_CPPFLAGS)

force:
$(tls_s2n_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
