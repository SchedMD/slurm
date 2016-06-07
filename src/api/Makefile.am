#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

if WITH_GNU_LD
VERSION_SCRIPT = \
	version.map
OTHER_FLAGS = \
	-Wl,--version-script=$(VERSION_SCRIPT)
PMI_VERSION_SCRIPT = \
	pmi_version.map
PMI_OTHER_FLAGS = \
	-Wl,--version-script=$(PMI_VERSION_SCRIPT)
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

# libpmi version information
#
# The libpmi interface shouldn't be changing any time soon, so for SLURM's
# libpmi only the library REVISION and AGE should change (and it is debatable
# whether these parts of the .so version should change).
#
# REVISION is changed if the source of the library has changed so linkers
# prefer the new version.
# AGE is changed if interfaces are added but compatibility is not broken.
libpmi_current = 0
libpmi_age     = 0
libpmi_rev     = 0

lib_LTLIBRARIES = libslurm.la libpmi.la

# BUILT_SOURCES tells automake that when "make" is called, these targets
# need to be built before anything else.  For instance, this takes care
# of libpmi.la's dependency on libslurm.la, as long as you use "make"
# and NOT "make libpmi.la".
BUILT_SOURCES = $(VERSION_SCRIPT) $(PMI_VERSION_SCRIPT) libslurm.la

# Note that libslurmhelper is mostly the same as libslurm, except that
# it exports ALL symbols used by the process, libcommon, libeio, etc.
# Only link with libslurmhelper if you are sure you are not going to be
# loading a plugin that could use something you yourself are not
# calling from here.
# libslurm.o only contains all the api symbols and will export
# them to plugins that are loaded.
# Also, libslurmhelper, libslurm.o are for convenience, they are not installed.
noinst_LTLIBRARIES = libslurmhelper.la
noinst_PROGRAMS = libslurm.o
# This is needed if compiling on windows
EXEEXT=

slurmapi_src =           \
	allocate.c       \
	allocate_msg.c   \
	block_info.c     \
	burst_buffer_info.c \
	assoc_mgr_info.c    \
	cancel.c         \
	checkpoint.c     \
	complete.c       \
	config_info.c    \
	federation_info.c \
	front_end_info.c \
	init_msg.c       \
	job_info.c       \
	job_step_info.c  \
	layout_info.c    \
	license_info.c   \
	node_info.c      \
	partition_info.c \
	pmi_server.c     \
	pmi_server.h     \
	powercap_info.c  \
	reservation_info.c \
	signal.c         \
	slurm_get_statistics.c \
	slurm_hostlist.c \
	slurm_pmi.c      \
	slurm_pmi.h	 \
	step_ctx.c       \
	step_ctx.h       \
	step_io.c        \
	step_io.h        \
	step_launch.c    \
	step_launch.h    \
	submit.c         \
	suspend.c        \
	topo_info.c      \
	triggers.c       \
	reconfigure.c    \
	update_config.c

common_dir = $(top_builddir)/src/common

slurmapi_add = 			 \
	$(common_dir)/libcommon.la	\
	$(common_dir)/libspank.la      	\
	$(common_dir)/libeio.la		\
	-lpthread

libslurmhelper_la_SOURCES = $(slurmapi_src)
libslurmhelper_la_LIBADD = $(slurmapi_add)
libslurmhelper_la_LDFLAGs = \
	$(LIB_LDFLAGS) \
	-version-info $(current):$(rev):$(age)

convenience_libs = \
	$(top_builddir)/src/api/libslurmhelper.la

libslurm_la_SOURCES =
libslurm_la_LIBADD = $(convenience_libs)
libslurm_la_LDFLAGS        = \
        $(LIB_LDFLAGS) \
        -version-info $(current):$(rev):$(age) \
        $(OTHER_FLAGS)
#
# The libpmi_la_LIBADD specification below causes libpmi.la to relink
# when running "make install", but removing it prevents essential slurm
# symbols from being available in programs linking with libpmi
#
libpmi_la_SOURCES = pmi.c
libpmi_la_LIBADD = $(top_builddir)/src/api/libslurm.la
libpmi_la_LDFLAGS = $(LIB_LDFLAGS) \
        -version-info $(libpmi_current):$(libpmi_rev):$(libpmi_age) \
        $(PMI_OTHER_FLAGS)

# This was made so we could export all symbols from libcommon
# on multiple platforms
libslurm_o_SOURCES =

libslurm_o_LDADD = $(top_builddir)/src/common/libeio.o	\
	$(top_builddir)/src/common/libspank.o	\
	$(top_builddir)/src/common/libcommon.o

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
	 echo "  local: *;"; \
	 echo "};") > $(VERSION_SCRIPT)

$(PMI_VERSION_SCRIPT) :
	(echo "{ global:"; \
	 echo "   PMI_*;"; \
	 echo "  local: *;"; \
	 echo "};") > $(PMI_VERSION_SCRIPT)

CLEANFILES = \
	$(VERSION_SCRIPT) $(PMI_VERSION_SCRIPT)

DISTCLEANFILES = \
	$(VERSION_SCRIPT) $(PMI_VERSION_SCRIPT)
