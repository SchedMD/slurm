# $Id$
#
# Makefile for slurmlib

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = allocate build_info cancel node_info partition_info reconfigure submit update_config
else
test_modules = 
endif

noinst_LIBRARIES = libslurmapi.a

libslurmapi_a_SOURCES = allocate.c build_info.c node_info.c partition_info.c submit.c reconfigure.c update_config.c

LDADD = $(top_srcdir)/src/common/libcommon.a

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#allocate_CFLAGS        = -DDEBUG_MODULE $(AM_CFLAGS)
#build_info_CFLAGS      = -DDEBUG_MODULE $(AM_CFLAGS)
#cancel_CFLAGS          = -DDEBUG_MODULE $(AM_CFLAGS)
#node_info_CFLAGS       = -DDEBUG_MODULE $(AM_CFLAGS)
#partition_info_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)
#reconfigure_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)
#submit_CFLAGS          = -DDEBUG_MODULE $(AM_CFLAGS)
#update_confige_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
allocate       : allocate_d.o
build_info     : build_info_d.o
cancel         : cancel_d.o
node_info      : node_info_d.o
partition_info : partition_info_d.o
reconfigure    : reconfigure_d.o
submit         : submit_d.o
update_config  : update_config_d.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
