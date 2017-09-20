# Makefile for ext_sensors/rrd plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common  $(RRDTOOL_CPPFLAGS)

if BUILD_RRD
PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = ext_sensors_rrd.la

# external sensors plugin.
ext_sensors_rrd_la_SOURCES = ext_sensors_rrd.c \
	ext_sensors_rrd.h

ext_sensors_rrd_la_LDFLAGS = $(PLUGIN_FLAGS) $(RRDTOOL_LDFLAGS)
ext_sensors_rrd_la_LIBADD = $(RRDTOOL_LIBS)

else

EXTRA_ext_sensors_rrd_la_SOURCES = ext_sensors_rrd.c \
				   ext_sensors_rrd.h

endif
