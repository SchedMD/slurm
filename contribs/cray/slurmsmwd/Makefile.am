#
# Makefile for cray tools
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

sbin_PROGRAMS = slurmsmwd
slurmsmwd_SOURCES	= main.c read_config.c
slurmsmwd_LDADD = $(LIB_SLURM) $(DL_LIBS)
slurmsmwd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)


ETC_FILES = slurmsmwd.service

CLEANFILES = $(ETC_FILES)

edit = sed \
        -e 's|@bindir[@]|$(bindir)|g' \
        -e 's|@libdir[@]|$(libdir)|g' \
        -e 's|@sbindir[@]|$(sbindir)|g' \
        -e 's|@sysconfdir[@]|$(sysconfdir)|g'

noinst_DATA = $(ETC_FILES)

slurmsmwd.service: Makefile $(srcdir)/slurmsmwd.service.in
	$(edit) $(srcdir)/slurmsmwd.service.in > slurmsmwd.service

force:
$(slurmsmwd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
