AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

SRCS = \
	alloc.c \
	anchor.c \
	commands.c \
	lua.c \
	rpc.c \
	scrun.c	\
	scrun.h \
	srun.c \
	state.c

convenience_libs = $(LIB_SLURM)

if HAVE_LUA

AM_CPPFLAGS = -I$(top_srcdir) $(lua_CFLAGS) -I$(top_srcdir)/src/scrun/

bin_PROGRAMS = scrun
scrun_SOURCES = $(SRCS)
scrun_DEPENDENCIES = $(LIB_SLURM_BUILD)
scrun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
scrun_LDADD = $(convenience_libs) $(top_builddir)/src/lua/libslurm_lua.la $(lua_LIBS)

REF = usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_scrun_DEPENDENCIES = $(LIB_REF)
scrun_LDADD += $(LIB_REF)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_scrun_SOURCES = $(SRCS)
endif
