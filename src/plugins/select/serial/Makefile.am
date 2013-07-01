# Makefile for select/serial plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_serial.la

# serial job resource selection plugin.
select_serial_la_SOURCES =  select_serial.c select_serial.h \
                            dist_tasks.c dist_tasks.h \
			    job_test.c job_test.h
select_serial_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
