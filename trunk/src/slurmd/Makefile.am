# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = get_mach_stat read_proc
else
test_modules = 
endif

bin_PROGRAMS = slurmd

# uncomment when we are ready to build a slurmd
# also, be sure to fill out slurmd_SOURCES
# bin_PROGRAMS = slurmd

noinst_PROGRAMS = $(test_modules)

LDADD = $(top_srcdir)/src/common/libcommon.la

slurmd_SOURCES = get_mach_stat.c \
                 read_proc.c

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#get_mach_stat_CFLAGS = -DDEBUG_MODULE $(AM_CFLAGS)
#read_proc_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to be built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
get_mach_stat : get_mach_stat_d.o
read_proc     : read_proc_d.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 

