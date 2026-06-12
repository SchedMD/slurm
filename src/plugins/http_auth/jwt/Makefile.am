AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir)

pkglib_LTLIBRARIES = http_auth_jwt.la

http_auth_jwt_la_SOURCES = http_auth_jwt.c
http_auth_jwt_la_DEPENDENCIES =
http_auth_jwt_la_LDFLAGS = $(PLUGIN_FLAGS)
http_auth_jwt_la_LIBADD =
http_auth_jwt_la_CFLAGS =

force:
$(http_auth_jwt_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
