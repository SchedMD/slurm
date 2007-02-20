# $Id: Makefile.am 5062 2005-06-21 01:50:29Z morrone $
# Makefile for auth/krb5 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(KRB5_CPPFLAGS)

# Add your plugin to this line, following the naming conventions.
if WITH_KRB5
KRB5 = auth_krb5.la
endif

pkglib_LTLIBRARIES = $(KRB5)

# Kerberos v5 authentication plugin
auth_krb5_la_SOURCES = auth_krb5.c
auth_krb5_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(KRB5_LDFLAGS)
auth_krb5_la_LIBADD =  $(KRB5_LIBS)
