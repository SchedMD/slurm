# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = Controller Node_Mgr Partition_Mgr Read_Config 
else
test_modules = 
endif

bin_PROGRAMS = slurmctld

noinst_PROGRAMS = $(test_modules)

LDADD = $(top_srcdir)/src/common/libcommon.a

slurmctld_SOURCES = Controller.c \
                    Mach_Stat_Mgr.c \
		    Partition_Mgr.c \
		    Read_Config.c 

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#Controller_CFLAGS    = -DDEBUG_MODULE $(AM_CFLAGS)
#Mach_Stat_Mgr_CFLAGS = -DDEBUG_MODULE $(AM_CFLAGS)
#Partition_Mgr_CFLAGS = -DDEBUG_MODULE $(AM_CFLAGS)
#Read_Config_CFLAGS   = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
Read_Config   : Read_Config_d.o Node_Mgr.o Partition_Mgr.o
Controller    : Controller_d.o
Node_Mgr      : Node_Mgr_d.o
Partition_Mgr : Partition_Mgr_d.o

% : %_d.o
	$(LINK) $(LDFLAGS) $< $(LDADD)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
