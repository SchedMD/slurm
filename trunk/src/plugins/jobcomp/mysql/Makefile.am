# Makefile for jobcomp/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_MYSQL
pkglib_LTLIBRARIES = jobcomp_mysql.la

# Mysql storage plugin.
jobcomp_mysql_la_SOURCES = jobcomp_mysql.c \
			mysql_jobcomp_process.c mysql_jobcomp_process.h
jobcomp_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobcomp_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
jobcomp_mysql_la_LIBADD  = $(top_builddir)/src/database/libslurm_mysql.la \
	$(MYSQL_LIBS)
jobcomp_mysql_la_DEPENDENCIES = $(top_builddir)/src/database/libslurm_mysql.la
else
EXTRA_jobcomp_mysql_la_SOURCES = jobcomp_mysql.c \
		mysql_jobcomp_process.c mysql_jobcomp_process.h
endif
