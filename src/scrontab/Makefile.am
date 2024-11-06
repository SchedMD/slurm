AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

SRCS = \
	env.c \
	opt.c \
	parse.c \
	scrontab.c \
	scrontab.h

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scrontab

scrontab_SOURCES = $(SRCS)
scrontab_DEPENDENCIES = $(LIB_SLURM_BUILD)
scrontab_LDFLAGS = $(CMD_LDFLAGS)

scrontab_LDADD = $(LIB_SLURM)

REF = default_crontab.txt usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_scrontab_DEPENDENCIES = $(LIB_REF)
scrontab_LDADD += $(LIB_REF)

force:
$(scrontab_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
