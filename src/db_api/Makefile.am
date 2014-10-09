#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

if WITH_GNU_LD
VERSION_SCRIPT = \
	version.map
OTHER_FLAGS = \
	-Wl,--version-script=$(VERSION_SCRIPT)
endif

# libslurmdb version information :
#
# Update META file information before a public release only!
#
# Note that libtool builds libslurmdb.so.<major>.<age>.<rev>
# where major = API_MAJOR = (API_CURRENT - API_AGE)
# and   age   = API_AGE
# and   rev   = API_REVISION
#
# The following examples are based upon initial values of API_CURRENT = 6,
# API_AGE = 4, API_REVISION = 6.
#
# If any interfaces have been added: increment API_CURRENT, increment API_AGE,
# and set API_REVISION = 0 (e.g. libslurmdb.so.2.4.6 -> libslurmdb.so.2.5.0)
#
# If any interfaces have been removed: increment API_CURRENT, set API_AGE = 0,
# and set API_REVISION = 0 (e.g. libslurmdb.so.2.4.6 -> libslurmdb.so.7.0.0)
#
# If any interfaces have changed: increment API_CURRENT only
# (e.g. libslurmdb.so.2.4.6 -> libslurmdb.so.3.4.6)
#
# If the interfaces have not changed, but underlying code has changed at all:
# increment API_REVISION only (e.g. libslurmdb.so.2.4.6 -> libslurmdb.so.2.4.7)
#
current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

lib_LTLIBRARIES = libslurmdb.la

# BUILT_SOURCES tells automake that when "make" is called, these targets
# need to be built before anything else.
BUILT_SOURCES = $(VERSION_SCRIPT) libslurmdb.la

# libslurmdb.o contains all the api symbols and will export
# them to plugins that are loaded.  Since you can not include both libslurm
# and libslurmdb in the same program because of extra symbols this is needed.
# libslurmdb.o are for convenience, and it is not installed.
noinst_PROGRAMS = libslurmdb.o
# This is needed if compiling on windows
EXEEXT=

libslurmdb_la_SOURCES =	\
	account_functions.c \
	archive_functions.c \
	assoc_functions.c \
	cluster_functions.c \
	cluster_report_functions.c \
	coord_functions.c \
	connection_functions.c \
	extra_get_functions.c \
	job_report_functions.c \
	qos_functions.c \
	resource_functions.c \
	resv_report_functions.c \
	usage_functions.c \
	user_functions.c \
	user_report_functions.c \
	wckey_functions.c

libslurmdb_la_LIBADD = $(top_builddir)/src/api/libslurmhelper.la
libslurmdb_la_LDFLAGS        = \
        $(LIB_LDFLAGS) \
        -version-info $(current):$(rev):$(age) \
        $(OTHER_FLAGS)

# This was made so we could export all symbols from libcommon
# on multiple platforms
libslurmdb_o_SOURCES =

libslurmdb_o_LDADD = $(top_builddir)/src/api/libslurm.o

libslurmdb.o$(EXEEXT): $(libslurmdb_la_OBJECTS) $(libslurmdb_o_LDADD)
	$(LINK) $(libslurmdb_la_OBJECTS) $(libslurmdb_o_LDADD)

force:
$(libslurmdb_o_LDADD) $(libslurmdb_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

$(VERSION_SCRIPT) :
	(echo "{ global:";   \
	 echo "   slurm_*;"; \
	 echo "   slurmdb_*;"; \
	 echo "   working_cluster_rec;"; \
	 echo "  local: *;"; \
	 echo "};") > $(VERSION_SCRIPT)

CLEANFILES = \
	$(VERSION_SCRIPT)

DISTCLEANFILES = \
	$(VERSION_SCRIPT)
