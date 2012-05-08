# Makefile.am for libpermapi for poe to interface with

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

# making a .la

pkglib_LTLIBRARIES = libpermapi.la

CPPFLAGS = -DMYSELF_SO=\"$(libdir)/slurm/libpermapi.so\"

convenience_libs = \
	$(top_builddir)/src/api/libslurm.o -ldl -lpthread

srun_dir = $(top_srcdir)/src/srun

libpermapi_la_SOURCES = shr_64.c \
	$(srun_dir)/opt.c \
	$(srun_dir)/srun_job.c \
	$(srun_dir)/srun_pty.c \
	$(srun_dir)/debugger.c    \
	$(srun_dir)/fname.c     \
	$(srun_dir)/allocate.c  \
	$(srun_dir)/multi_prog.c \
	$(srun_dir)/launch.c

libpermapi_la_LDFLAGS = -export-dynamic $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
	$(convenience_libs)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
