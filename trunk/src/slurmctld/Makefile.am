# $Id$
#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = bits_bytes controller node_mgr partition_mgr read_config 
else
test_modules = 
endif

bin_PROGRAMS = slurmctld

noinst_PROGRAMS = $(test_modules)

LDADD = $(top_srcdir)/src/common/libcommon.a

slurmctld_SOURCES = bits_bytes.c \
		    controller.c \
		    node_mgr.c \
		    partition_mgr.c \
		    read_config.c 

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#bits_bytes_CFLAGS    = -DDEBUG_MODULE $(AM_CFLAGS)
#controller_CFLAGS    = -DDEBUG_MODULE $(AM_CFLAGS)
#node_mgr_CFLAGS      = -DDEBUG_MODULE $(AM_CFLAGS)
#partition_mgr_CFLAGS = -DDEBUG_MODULE $(AM_CFLAGS)
#read_config_CFLAGS   = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
bits_bytes    : bits_bytes_d.o
controller    : controller_d.o bits_bytes.o node_mgr.o partition_mgr.o read_config.o 
node_mgr      : node_mgr_d.o bits_bytes.o
partition_mgr : partition_mgr_d.o bits_bytes.o node_mgr.o
read_config   : read_config_d.o bits_bytes.o node_mgr.o partition_mgr.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
