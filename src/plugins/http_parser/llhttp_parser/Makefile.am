AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(LLHTTP_PARSER_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) $(LLHTTP_PARSER_CPPFLAGS)

pkglib_LTLIBRARIES = http_parser_llhttp_parser.la

http_parser_llhttp_parser_la_SOURCES = llhttp_parser.c
http_parser_llhttp_parser_la_DEPENDENCIES =
http_parser_llhttp_parser_la_LDFLAGS = $(PLUGIN_FLAGS)
http_parser_llhttp_parser_la_LIBADD = ../common/libhttp_parser_common.la
http_parser_llhttp_parser_la_CFLAGS =

force:
$(http_parser_llhttp_parser_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
