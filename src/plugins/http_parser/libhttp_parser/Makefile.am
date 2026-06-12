AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(HTTP_PARSER_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) $(HTTP_PARSER_CPPFLAGS)

pkglib_LTLIBRARIES = http_parser_libhttp_parser.la

http_parser_libhttp_parser_la_SOURCES = libhttp_parser.c
http_parser_libhttp_parser_la_DEPENDENCIES =
http_parser_libhttp_parser_la_LDFLAGS = $(PLUGIN_FLAGS)
http_parser_libhttp_parser_la_LIBADD =
http_parser_libhttp_parser_la_CFLAGS =

force:
$(http_parser_libhttp_parser_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
