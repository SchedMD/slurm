# $Id$
# Makefile for common library 

AUTOMAKE_OPTIONS = foreign

INCLUDES     = -I$(top_srcdir) $(SSL_CPPFLAGS) 

if WITH_ELAN
elan_sources = qsw.c qsw.h
else
elan_sources = 
endif

noinst_LTLIBRARIES = 	\
	libcommon.la 	\
	libdaemonize.la \
	libhostlist.la	\
	libeio.la  	


libcommon_la_SOURCES = 			\
	xmalloc.c xmalloc.h 		\
	xassert.c xassert.h		\
	xstring.c xstring.h		\
	xsignal.c xsignal.h		\
	strlcpy.c strlcpy.h		\
	list.c list.h 			\
	fd.c fd.h			\
	log.c log.h			\
	cbuf.c cbuf.h			\
	safeopen.c safeopen.h		\
	bitstring.c bitstring.h 	\
	pack.c pack.h			\
	parse_spec.c parse_spec.h	\
	plugin.c plugin.h		\
	plugrack.c plugrack.h		\
	read_config.c read_config.h	\
	slurm_cred.h       		\
	slurm_cred.c			\
	slurm_errno.c			\
	slurm_protocol_api.c		\
	slurm_protocol_api.h		\
	slurm_protocol_pack.c		\
	slurm_protocol_pack.h		\
	slurm_protocol_util.c		\
	slurm_protocol_util.h		\
	slurm_protocol_socket_implementation.c	\
	slurm_protocol_socket_common.h	\
	slurm_protocol_common.h		\
	slurm_protocol_interface.h	\
	slurm_protocol_defs.c		\
	slurm_protocol_defs.h		\
	util-net.c util-net.h		\
	slurm_auth.c slurm_auth.h	\
	arg_desc.c arg_desc.h		\
	macros.h			\
	$(elan_sources) 

libdaemonize_la_SOURCES =  \
	daemonize.c        \
	daemonize.h        \
	fd.c fd.h


libhostlist_la_SOURCES   = \
	hostlist.c hostlist.h

libeio_la_SOURCES = 	   \
	eio.c eio.h	   \
	io_hdr.c io_hdr.h


EXTRA_libcommon_la_SOURCES = \
	qsw.c qsw.h

libcommon_la_LIBADD   = $(SSL_LIBS) $(ELAN_LIBS)
libcommon_la_LDFLAGS  = $(SSL_LDFLAGS)	
