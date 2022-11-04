#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

if WITH_GNU_LD
VERSION_SCRIPT = \
	version.map
OTHER_FLAGS = \
	-Wl,--version-script=$(VERSION_SCRIPT)
FULL_VERSION_SCRIPT = \
	full_version.map
FULL_OTHER_FLAGS = \
	-Wl,--version-script=$(FULL_VERSION_SCRIPT)
SLURM_PMI_OTHER_FLAGS = \
	-Wl,--version-script=$(VERSION_SCRIPT)
endif

# libslurm version information :
#
# Update META file information before a public release only!
#
# Note that libtool builds libslurm.so.<major>.<age>.<rev>
# where major = API_MAJOR = (API_CURRENT - API_AGE)
# and   age   = API_AGE
# and   rev   = API_REVISION
#
# The following examples are based upon initial values of API_CURRENT = 6,
# API_AGE = 4, API_REVISION = 6.
#
# If any interfaces have been added: increment API_CURRENT, increment API_AGE,
# and set API_REVISION = 0 (e.g. libslurm.so.2.4.6 -> libslurm.so.2.5.0)
#
# If any interfaces have been removed: increment API_CURRENT, set API_AGE = 0,
# and set API_REVISION = 0 (e.g. libslurm.so.2.4.6 -> libslurm.so.7.0.0)
#
# If any interfaces have changed: increment API_CURRENT only
# (e.g. libslurm.so.2.4.6 -> libslurm.so.3.4.6)
#
# If the interfaces have not changed, but underlying code has changed at all:
# increment API_REVISION only (e.g. libslurm.so.2.4.6 -> libslurm.so.2.4.7)
#
current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

lib_LTLIBRARIES = libslurm.la

# The ABI of libslurmfull can easily change from 1 micro version to another.
# To avoid issue we will install this lib alongside the plugins to protect
# from version issues.
#
# The unversioned libslurm_pmi is hidden there as well.
pkglib_LTLIBRARIES = libslurmfull.la libslurm_pmi.la

# BUILT_SOURCES tells automake that when "make" is called, these targets
# need to be built before anything else.
BUILT_SOURCES = $(VERSION_SCRIPT) $(FULL_VERSION_SCRIPT) libslurm.la

# Note that libslurmhelper is mostly the same as libslurm, except that
# it exports ALL symbols used by the process, libcommon, etc.
# Only link with libslurmhelper if you are sure you are not going to be
# loading a plugin that could use something you yourself are not
# calling from here.
# libslurm.o only contains all the api symbols and will export
# them to plugins that are loaded.
# Also, libslurmhelper, libslurm.o are for convenience, they are not installed.
noinst_LTLIBRARIES = libslurmhelper.la
noinst_PROGRAMS = libslurm.o

slurmdbapi_src =	\
	account_functions.c \
	archive_functions.c \
	assoc_functions.c \
	cluster_functions.c \
	cluster_report_functions.c \
	coord_functions.c \
	connection_functions.c \
	extra_get_functions.c \
	federation_functions.c \
	job_functions.c \
	job_report_functions.c \
	qos_functions.c \
	resource_functions.c \
	tres_functions.c \
	usage_functions.c \
	user_functions.c \
	user_report_functions.c \
	wckey_functions.c

slurmapi_src =           \
	allocate.c       \
	allocate_msg.c   \
	block_info.c     \
	burst_buffer_info.c \
	assoc_mgr_info.c    \
	cancel.c         \
	complete.c       \
	config_info.c    \
	crontab.c        \
	federation_info.c \
	front_end_info.c \
	init.c           \
	init_msg.c       \
	job_info.c       \
	job_step_info.c  \
	license_info.c   \
	node_info.c      \
	partition_info.c \
	pmi_server.c     \
	pmi_server.h     \
	reservation_info.c \
	signal.c         \
	slurm_get_statistics.c \
	slurm_pmi.c      \
	slurm_pmi.h	 \
	step_io.c        \
	step_io.h        \
	step_launch.c    \
	step_launch.h    \
	submit.c         \
	suspend.c        \
	token.c          \
	topo_info.c      \
	triggers.c       \
	reconfigure.c    \
	update_config.c \
	$(slurmdbapi_src)

common_dir = $(top_builddir)/src/common

slurmapi_add = 			 \
	$(common_dir)/libcommon.la	\
	$(top_builddir)/src/interfaces/libcommon_interfaces.la \
	-lpthread

libslurmhelper_la_SOURCES = $(slurmapi_src)
libslurmhelper_la_LIBADD = $(slurmapi_add)
libslurmhelper_la_LDFLAGS = $(LIB_LDFLAGS)

convenience_libs = \
	$(top_builddir)/src/api/libslurmhelper.la

libslurm_la_SOURCES =
libslurm_la_LIBADD = $(convenience_libs)
libslurm_la_LDFLAGS        = \
        $(LIB_LDFLAGS) \
        -version-info $(current):$(rev):$(age) \
        $(OTHER_FLAGS)

libslurmfull_la_SOURCES =
libslurmfull_la_LIBADD = $(convenience_libs)
libslurmfull_la_LDFLAGS        = \
        $(LIB_LDFLAGS) \
	-avoid-version \
        $(FULL_OTHER_FLAGS)

libslurm_pmi_la_SOURCES =
libslurm_pmi_la_LIBADD = $(convenience_libs)
libslurm_pmi_la_LDFLAGS        = \
        $(LIB_LDFLAGS) \
	-avoid-version \
        $(SLURM_PMI_OTHER_FLAGS)

# This was made so we could export all symbols from libcommon
# on multiple platforms
libslurm_o_SOURCES =

libslurm_o_LDADD =				\
	$(top_builddir)/src/common/libcommon.o \
	$(top_builddir)/src/interfaces/libcommon_interfaces.o

libslurm.o$(EXEEXT): $(libslurmhelper_la_OBJECTS) $(libslurm_o_LDADD)
	$(LINK) $(libslurmhelper_la_OBJECTS) $(libslurm_o_LDADD)

force:
$(libslurm_o_LDADD) $(convenience_libs) $(slurmapi_add) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

# select/cray needs to load select/linear, thus plugin_context_ is required
$(VERSION_SCRIPT) :
	(echo "{ global:";   \
	 echo "   islurm_*;"; \
	 echo "   slurm_*;"; \
	 echo "   slurmdb_*;"; \
	 echo "   plugin_context_*;"; \
	 echo "   working_cluster_rec;"; \
	 echo "   node_record_count;"; \
	 echo "   active_node_record_count;"; \
	 echo "   node_record_table_ptr;"; \
	 echo "   switch_record_cnt;"; \
	 echo "   switch_levels;"; \
	 echo "   switch_record_table;"; \
	 echo "  local: *;"; \
	 echo "};") > $(VERSION_SCRIPT)

$(FULL_VERSION_SCRIPT) :
	(echo "{ global: *; };") > $(FULL_VERSION_SCRIPT)

CLEANFILES = \
	$(VERSION_SCRIPT) $(FULL_VERSION_SCRIPT)

DISTCLEANFILES = \
	$(VERSION_SCRIPT) $(FULL_VERSION_SCRIPT)
