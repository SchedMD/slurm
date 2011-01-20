# Makefile for switch/elan plugin

AUTOMAKE_OPTIONS = foreign

if HAVE_ELAN
elan_lib = switch_elan.la
else
elan_lib =
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = $(elan_lib)

if HAVE_ELAN
# Quadrics elan3 or elan4 switch plugin.
switch_elan_la_SOURCES =        \
	switch_elan.c		\
	qsw.c qsw.h

# Add libslurm to switch_elan to provide some symbols that are not
#  available in slurmctld (slurm_setenvpf)

switch_elan_la_LIBADD  = $(ELAN_LIBS) \
			$(top_builddir)/src/api/libslurm.la

switch_elan_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_switch_elan_la_SOURCES =  \
	switch_elan.c           \
	qsw.c qsw.h
endif
