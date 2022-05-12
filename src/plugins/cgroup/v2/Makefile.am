# Makefile for cgroup/v2 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	      $(dbus_CFLAGS)

pkglib_LTLIBRARIES = cgroup_v2.la

# Cgroup v2 plugin.
cgroup_v2_la_SOURCES =	\
	cgroup_v2.c	\
	ebpf.c		\
	ebpf.h		\
	cgroup_dbus.c	\
	cgroup_dbus.h

cgroup_v2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
cgroup_v2_la_LIBADD = ../common/libcgroup_common.la $(dbus_LIBS)
