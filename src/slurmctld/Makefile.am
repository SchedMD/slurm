# Makefile for slurmctld

INCLUDES = @CPPFLAGS@ -I$(top_srcdir)/src/common

bin_PROGRAMS = slurmctld

slurmctld_SOURCES = Controller.c Mach_Stat_Mgr.c Partition_Mgr.c Read_Config.c

slurmctld_LDFLAGS =
slurmctld_LDADD = -L$(top_srcdir)/src/libcommon.a
