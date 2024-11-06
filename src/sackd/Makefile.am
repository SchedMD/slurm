AUTOMAKE_OPTIONS = foreign

SRCS = sackd.c

AM_CPPFLAGS = -I$(top_srcdir)

sbin_PROGRAMS = sackd

sackd_SOURCES = $(SRCS)
sackd_DEPENDENCIES = $(LIB_SLURM_BUILD)
sackd_LDFLAGS = $(CMD_LDFLAGS)

sackd_LDADD = $(LIB_SLURM)

REF = usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_sackd_DEPENDENCIES = $(LIB_REF)
sackd_LDADD += $(LIB_REF)

force:
$(sackd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
