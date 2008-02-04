#
# Makefile for slurmdbd

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

sbin_PROGRAMS = slurmdbd

slurmdbd_LDADD = 					\
	$(top_builddir)/src/common/libcommon.la    	\
	$(top_builddir)/src/common/libdaemonize.la


slurmdbd_SOURCES = 	\
	agent.c		\
	agent.h		\
	read_config.c	\
	read_config.h	\
	rpc_pack.c	\
	rpc_pack.h	\
	slurmdbd.c  	\
	slurmdbd.h	\

slurmdbd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
			$(FEDERATION_LDFLAGS)	

force:
$(slurmdbd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
