# $Id$
# Makefile for auth plugins

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

# Add your plugin to this line, following the naming conventions.
if WITH_AUTHD
AUTHD = auth_authd.la
endif
if WITH_MUNGE
MUNGE = auth_munge.la
endif

pkglib_LTLIBRARIES = auth_none.la $(AUTHD) $(MUNGE)

# Null authentication plugin.
auth_none_la_SOURCES = auth_none.c
auth_none_la_LDFLAGS = $(PLUGIN_FLAGS)
auth_none_la_LIBADD = $(top_builddir)/src/api/libslurm.la

# Authd authentication plugin -- requires libauth
auth_authd_la_SOURCES = auth_authd.c
auth_authd_la_LDFLAGS = $(PLUGIN_FLAGS) 
auth_authd_la_LIBADD =  $(top_builddir)/src/api/libslurm.la -lauth 

# Munge authentication plugin
auth_munge_la_SOURCES = auth_munge.c
auth_munge_la_LDFLAGS = $(PLUGIN_FLAGS) 
auth_munge_la_LIBADD =  $(top_builddir)/src/api/libslurm.la -lmunge 


