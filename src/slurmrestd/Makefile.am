if WITH_SLURMRESTD
SUBDIRS = plugins
endif

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

SRCS = \
	http.c http.h \
	operations.c operations.h \
	slurmrestd.c \
	rest_auth.h rest_auth.c

if WITH_SLURMRESTD
AM_CPPFLAGS = -I$(top_srcdir) $(HTTP_PARSER_CPPFLAGS)

sbin_PROGRAMS = slurmrestd

slurmrestd_SOURCES = $(SRCS)
slurmrestd_DEPENDENCIES = $(LIB_SLURM_BUILD)
slurmrestd_LDFLAGS = $(CMD_LDFLAGS)

slurmrestd_LDADD = $(LIB_SLURM) $(HTTP_PARSER_LDFLAGS)

REF = usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_slurmrestd_DEPENDENCIES = $(LIB_REF)
slurmrestd_LDADD += $(LIB_REF)

endif
force:
$(slurmrestd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
